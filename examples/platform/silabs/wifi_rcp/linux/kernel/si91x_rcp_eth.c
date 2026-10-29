// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * SiWx917 SoC SDIO Ethernet driver.
 *
 * Exposes the Wi-Fi link owned by the Matter stack running on the SiWx917 M4
 * as a plain Ethernet interface. The host has no control over the Wi-Fi link:
 * carrier follows the 917 connection state and the MAC is dictated by the 917.
 */
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

#include "wifi_rcp_protocol.h"

#define DRV_NAME "si91x_rcp_eth"

#ifndef SI91X_RCP_SDIO_VENDOR_ID
#define SI91X_RCP_SDIO_VENDOR_ID 0x041B
#endif
#ifndef SI91X_RCP_SDIO_DEVICE_ID
#define SI91X_RCP_SDIO_DEVICE_ID 0x0917
#endif

// Width of the block count field in WIFI_RCP_SDIO_REG_RX_NUM_BLOCKS.
#define RCP_RX_BLOCKS_MASK 0x1F
#define RCP_RX_BUF_SIZE (RCP_RX_BLOCKS_MASK * WIFI_RCP_BLOCK_SIZE)
#define RCP_TX_BUF_SIZE round_up(WIFI_RCP_HDR_SIZE + ETH_FRAME_LEN, WIFI_RCP_BLOCK_SIZE)

#define RCP_TXQ_STOP_LEN 64
#define RCP_TXQ_WAKE_LEN 16
#define RCP_IRQ_MAX_XFERS 16
#define RCP_REQ_INTERVAL msecs_to_jiffies(2000)

enum rcp_flags
{
    RCP_FLAG_REQ_PENDING,
    RCP_FLAG_HAVE_STATUS,
};

struct rcp_priv
{
    struct sdio_func * func;
    struct net_device * ndev;
    struct workqueue_struct * wq;

    struct sk_buff_head txq;
    struct work_struct tx_work;
    struct work_struct status_work;
    struct delayed_work req_work;
    unsigned long flags;

    spinlock_t status_lock;
    wifi_rcp_link_status_t status;
    wifi_rcp_link_status_t pending_status;

    u8 * rx_buf;
    u8 * tx_buf;
};

static void rcp_set_hw_addr(struct net_device * ndev, const u8 * mac)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    eth_hw_addr_set(ndev, mac);
#else
    memcpy(ndev->dev_addr, mac, ETH_ALEN);
#endif
}

static void rcp_netif_rx(struct sk_buff * skb)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
    netif_rx(skb);
#else
    netif_rx_ni(skb);
#endif
}

/* ---------------------------------------------------------------------------
 * Device -> host
 * ------------------------------------------------------------------------- */

static void rcp_rx_frame(struct rcp_priv * priv, const u8 * data, u16 len)
{
    struct net_device * ndev = priv->ndev;
    struct sk_buff * skb;

    if (len < ETH_HLEN || len > ETH_FRAME_LEN)
    {
        ndev->stats.rx_length_errors++;
        return;
    }
    if (!netif_running(ndev))
    {
        return;
    }

    skb = netdev_alloc_skb_ip_align(ndev, len);
    if (!skb)
    {
        ndev->stats.rx_dropped++;
        return;
    }
    skb_put_data(skb, data, len);
    skb->protocol = eth_type_trans(skb, ndev);

    ndev->stats.rx_packets++;
    ndev->stats.rx_bytes += len;
    rcp_netif_rx(skb);
}

static void rcp_rx_link_status(struct rcp_priv * priv, const u8 * data, u16 len)
{
    if (len < sizeof(wifi_rcp_link_status_t))
    {
        priv->ndev->stats.rx_length_errors++;
        return;
    }

    spin_lock_bh(&priv->status_lock);
    memcpy(&priv->pending_status, data, sizeof(priv->pending_status));
    spin_unlock_bh(&priv->status_lock);

    // rtnl cannot be taken from the SDIO IRQ thread, it holds the host.
    queue_work(priv->wq, &priv->status_work);
}

static void rcp_handle_xfer(struct rcp_priv * priv, const u8 * buf, u32 xfer_len)
{
    u16 len = get_unaligned_le16(buf);
    u8 type = buf[2];

    if (WIFI_RCP_HDR_SIZE + (u32) len > xfer_len)
    {
        priv->ndev->stats.rx_frame_errors++;
        return;
    }

    switch (type)
    {
    case WIFI_RCP_MSG_ETH_FRAME:
        rcp_rx_frame(priv, buf + WIFI_RCP_HDR_SIZE, len);
        break;
    case WIFI_RCP_MSG_LINK_STATUS:
        rcp_rx_link_status(priv, buf + WIFI_RCP_HDR_SIZE, len);
        break;
    default:
        priv->ndev->stats.rx_frame_errors++;
        break;
    }
}

// Runs in the MMC core IRQ thread with the host already claimed.
static void rcp_sdio_irq(struct sdio_func * func)
{
    struct rcp_priv * priv = sdio_get_drvdata(func);
    int i;

    for (i = 0; i < RCP_IRQ_MAX_XFERS; i++)
    {
        int ret;
        u8 isr, blocks;
        u32 len;

        isr = sdio_f0_readb(func, WIFI_RCP_SDIO_REG_INTR_STATUS, &ret);
        if (ret || !(isr & WIFI_RCP_SDIO_INTR_PKT_PENDING))
        {
            return;
        }

        blocks = sdio_f0_readb(func, WIFI_RCP_SDIO_REG_RX_NUM_BLOCKS, &ret) & RCP_RX_BLOCKS_MASK;
        if (ret || blocks == 0)
        {
            return;
        }

        len = blocks * WIFI_RCP_BLOCK_SIZE;
        ret = sdio_readsb(func, priv->rx_buf, len, len);
        if (ret)
        {
            priv->ndev->stats.rx_errors++;
            return;
        }
        rcp_handle_xfer(priv, priv->rx_buf, len);
    }
}

static void rcp_status_work(struct work_struct * work)
{
    struct rcp_priv * priv   = container_of(work, struct rcp_priv, status_work);
    struct net_device * ndev = priv->ndev;
    char * envp[]            = { "SI91X_RCP=status", NULL };
    wifi_rcp_link_status_t st;

    spin_lock_bh(&priv->status_lock);
    st = priv->pending_status;
    spin_unlock_bh(&priv->status_lock);

    rtnl_lock();
    if (is_valid_ether_addr(st.mac) && !ether_addr_equal(ndev->dev_addr, st.mac))
    {
        rcp_set_hw_addr(ndev, st.mac);
        call_netdevice_notifiers(NETDEV_CHANGEADDR, ndev);
    }
    if (st.link_up)
    {
        netif_carrier_on(ndev);
    }
    else
    {
        netif_carrier_off(ndev);
    }
    rtnl_unlock();

    spin_lock_bh(&priv->status_lock);
    priv->status = st;
    spin_unlock_bh(&priv->status_lock);
    set_bit(RCP_FLAG_HAVE_STATUS, &priv->flags);

    kobject_uevent_env(&ndev->dev.kobj, KOBJ_CHANGE, envp);
}

/* ---------------------------------------------------------------------------
 * Host -> device
 * ------------------------------------------------------------------------- */

static int rcp_sdio_write(struct rcp_priv * priv, u32 padded_len)
{
    int ret;

    sdio_claim_host(priv->func);
    // The device derives the transfer size from the CMD53 address.
    ret = sdio_writesb(priv->func, padded_len, priv->tx_buf, padded_len);
    sdio_release_host(priv->func);
    return ret;
}

static void rcp_send_status_req(struct rcp_priv * priv)
{
    memset(priv->tx_buf, 0, WIFI_RCP_BLOCK_SIZE);
    put_unaligned_le16(0, priv->tx_buf);
    priv->tx_buf[2] = WIFI_RCP_MSG_LINK_STATUS_REQ;
    rcp_sdio_write(priv, WIFI_RCP_BLOCK_SIZE);
}

static void rcp_tx_work(struct work_struct * work)
{
    struct rcp_priv * priv   = container_of(work, struct rcp_priv, tx_work);
    struct net_device * ndev = priv->ndev;
    struct sk_buff * skb;

    if (test_and_clear_bit(RCP_FLAG_REQ_PENDING, &priv->flags))
    {
        rcp_send_status_req(priv);
    }

    while ((skb = skb_dequeue(&priv->txq)) != NULL)
    {
        u32 total  = WIFI_RCP_HDR_SIZE + skb->len;
        u32 padded = round_up(total, WIFI_RCP_BLOCK_SIZE);

        put_unaligned_le16(skb->len, priv->tx_buf);
        priv->tx_buf[2] = WIFI_RCP_MSG_ETH_FRAME;
        priv->tx_buf[3] = 0;
        skb_copy_bits(skb, 0, priv->tx_buf + WIFI_RCP_HDR_SIZE, skb->len);
        memset(priv->tx_buf + total, 0, padded - total);

        if (rcp_sdio_write(priv, padded))
        {
            ndev->stats.tx_errors++;
        }
        else
        {
            ndev->stats.tx_packets++;
            ndev->stats.tx_bytes += skb->len;
        }
        dev_consume_skb_any(skb);

        if (netif_queue_stopped(ndev) && skb_queue_len(&priv->txq) < RCP_TXQ_WAKE_LEN)
        {
            netif_wake_queue(ndev);
        }
    }

    if (netif_queue_stopped(ndev) && netif_carrier_ok(ndev))
    {
        netif_wake_queue(ndev);
    }
}

// Forwarding on the 917 is only enabled after a status request; repeat until answered.
static void rcp_req_work(struct work_struct * work)
{
    struct rcp_priv * priv = container_of(to_delayed_work(work), struct rcp_priv, req_work);

    if (test_bit(RCP_FLAG_HAVE_STATUS, &priv->flags))
    {
        return;
    }
    set_bit(RCP_FLAG_REQ_PENDING, &priv->flags);
    queue_work(priv->wq, &priv->tx_work);
    queue_delayed_work(priv->wq, &priv->req_work, RCP_REQ_INTERVAL);
}

/* ---------------------------------------------------------------------------
 * net_device
 * ------------------------------------------------------------------------- */

static int rcp_open(struct net_device * ndev)
{
    netif_start_queue(ndev);
    return 0;
}

static int rcp_stop(struct net_device * ndev)
{
    struct rcp_priv * priv = netdev_priv(ndev);

    netif_stop_queue(ndev);
    skb_queue_purge(&priv->txq);
    return 0;
}

static netdev_tx_t rcp_start_xmit(struct sk_buff * skb, struct net_device * ndev)
{
    struct rcp_priv * priv = netdev_priv(ndev);

    if (skb->len > ETH_FRAME_LEN)
    {
        ndev->stats.tx_dropped++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    skb_queue_tail(&priv->txq, skb);
    if (skb_queue_len(&priv->txq) >= RCP_TXQ_STOP_LEN)
    {
        netif_stop_queue(ndev);
    }
    queue_work(priv->wq, &priv->tx_work);
    return NETDEV_TX_OK;
}

// The MAC belongs to the 917, the host cannot change it.
static int rcp_set_mac_address(struct net_device * ndev, void * addr)
{
    return -EOPNOTSUPP;
}

static const struct net_device_ops rcp_netdev_ops = {
    .ndo_open            = rcp_open,
    .ndo_stop            = rcp_stop,
    .ndo_start_xmit      = rcp_start_xmit,
    .ndo_set_mac_address = rcp_set_mac_address,
    .ndo_validate_addr   = eth_validate_addr,
};

static void rcp_get_drvinfo(struct net_device * ndev, struct ethtool_drvinfo * info)
{
    struct rcp_priv * priv = netdev_priv(ndev);

    strscpy(info->driver, DRV_NAME, sizeof(info->driver));
    strscpy(info->bus_info, sdio_func_id(priv->func), sizeof(info->bus_info));
}

static const struct ethtool_ops rcp_ethtool_ops = {
    .get_drvinfo = rcp_get_drvinfo,
    .get_link    = ethtool_op_get_link,
};

static ssize_t rcp_status_show(struct device * dev, struct device_attribute * attr, char * buf)
{
    struct rcp_priv * priv = netdev_priv(to_net_dev(dev));
    wifi_rcp_link_status_t st;
    __be32 addr, mask, gw, dns;

    spin_lock_bh(&priv->status_lock);
    st = priv->status;
    spin_unlock_bh(&priv->status_lock);

    memcpy(&addr, &st.ipv4_addr, sizeof(addr));
    memcpy(&mask, &st.ipv4_netmask, sizeof(mask));
    memcpy(&gw, &st.ipv4_gateway, sizeof(gw));
    memcpy(&dns, &st.ipv4_dns, sizeof(dns));

    return sysfs_emit(buf, "link=%u\nmac=%pM\nipv4=%pI4\nnetmask=%pI4\ngateway=%pI4\ndns=%pI4\n", st.link_up ? 1 : 0, st.mac,
                      &addr, &mask, &gw, &dns);
}
static DEVICE_ATTR_RO(rcp_status);

static struct attribute * rcp_attrs[] = {
    &dev_attr_rcp_status.attr,
    NULL,
};

static const struct attribute_group rcp_attr_group = {
    .attrs = rcp_attrs,
};

/* ---------------------------------------------------------------------------
 * SDIO driver
 * ------------------------------------------------------------------------- */

static int rcp_probe(struct sdio_func * func, const struct sdio_device_id * id)
{
    struct net_device * ndev;
    struct rcp_priv * priv;
    int ret;

    ndev = alloc_etherdev(sizeof(*priv));
    if (!ndev)
    {
        return -ENOMEM;
    }
    SET_NETDEV_DEV(ndev, &func->dev);

    priv       = netdev_priv(ndev);
    priv->func = func;
    priv->ndev = ndev;
    skb_queue_head_init(&priv->txq);
    spin_lock_init(&priv->status_lock);
    INIT_WORK(&priv->tx_work, rcp_tx_work);
    INIT_WORK(&priv->status_work, rcp_status_work);
    INIT_DELAYED_WORK(&priv->req_work, rcp_req_work);

    priv->rx_buf = devm_kmalloc(&func->dev, RCP_RX_BUF_SIZE, GFP_KERNEL);
    priv->tx_buf = devm_kmalloc(&func->dev, RCP_TX_BUF_SIZE, GFP_KERNEL);
    if (!priv->rx_buf || !priv->tx_buf)
    {
        ret = -ENOMEM;
        goto err_free_netdev;
    }

    priv->wq = alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, sdio_func_id(func));
    if (!priv->wq)
    {
        ret = -ENOMEM;
        goto err_free_netdev;
    }

    ndev->netdev_ops     = &rcp_netdev_ops;
    ndev->ethtool_ops    = &rcp_ethtool_ops;
    ndev->sysfs_groups[0] = &rcp_attr_group;
    ndev->min_mtu        = ETH_MIN_MTU;
    ndev->max_mtu        = ETH_DATA_LEN;
    // Replaced by the 917 MAC on the first link status.
    eth_hw_addr_random(ndev);
    netif_carrier_off(ndev);

    sdio_set_drvdata(func, priv);

    sdio_claim_host(func);
    func->enable_timeout = 100;
    ret                  = sdio_enable_func(func);
    if (!ret)
    {
        ret = sdio_set_block_size(func, WIFI_RCP_BLOCK_SIZE);
    }
    sdio_release_host(func);
    if (ret)
    {
        dev_err(&func->dev, "SDIO function setup failed: %d\n", ret);
        goto err_disable;
    }

    ret = register_netdev(ndev);
    if (ret)
    {
        goto err_disable;
    }

    sdio_claim_host(func);
    ret = sdio_claim_irq(func, rcp_sdio_irq);
    sdio_release_host(func);
    if (ret)
    {
        dev_err(&func->dev, "sdio_claim_irq failed: %d\n", ret);
        goto err_unregister;
    }

    queue_delayed_work(priv->wq, &priv->req_work, 0);
    netdev_info(ndev, "SiWx917 SDIO Ethernet ready\n");
    return 0;

err_unregister:
    unregister_netdev(ndev);
err_disable:
    sdio_claim_host(func);
    sdio_disable_func(func);
    sdio_release_host(func);
    destroy_workqueue(priv->wq);
err_free_netdev:
    free_netdev(ndev);
    return ret;
}

static void rcp_remove(struct sdio_func * func)
{
    struct rcp_priv * priv   = sdio_get_drvdata(func);
    struct net_device * ndev = priv->ndev;

    sdio_claim_host(func);
    sdio_release_irq(func);
    sdio_release_host(func);

    cancel_delayed_work_sync(&priv->req_work);
    cancel_work_sync(&priv->status_work);
    unregister_netdev(ndev);
    cancel_work_sync(&priv->tx_work);
    destroy_workqueue(priv->wq);
    skb_queue_purge(&priv->txq);

    sdio_claim_host(func);
    sdio_disable_func(func);
    sdio_release_host(func);

    free_netdev(ndev);
}

static const struct sdio_device_id rcp_sdio_ids[] = {
    { SDIO_DEVICE(SI91X_RCP_SDIO_VENDOR_ID, SI91X_RCP_SDIO_DEVICE_ID) },
    {},
};
MODULE_DEVICE_TABLE(sdio, rcp_sdio_ids);

static struct sdio_driver rcp_sdio_driver = {
    .name     = DRV_NAME,
    .id_table = rcp_sdio_ids,
    .probe    = rcp_probe,
    .remove   = rcp_remove,
};
module_sdio_driver(rcp_sdio_driver);

MODULE_DESCRIPTION("SiWx917 SoC SDIO Ethernet driver for the Matter Wi-Fi RCP");
MODULE_LICENSE("Dual MIT/GPL");
