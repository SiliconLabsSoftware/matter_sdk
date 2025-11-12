# Testing Guide for Consolidated CSA Workflows

## Overview
This guide explains how to test the consolidated workflows:
- `silabs-update-branches.yaml` - Syncs CSA branches
- `silabs-open-csa-prs.yaml` - Creates PRs after sync

## Prerequisites
- Access to the GitHub repository
- Permissions to trigger workflows manually
- Access to repository secrets/variables:
  - `SILABSSW_MATTER_CI_BOT_APP_ID` (variable)
  - `SILABSSW_MATTER_CI_BOT_APP_PRIVATE_KEY` (secret)

## Testing Strategy

### Option 1: Manual Testing via GitHub UI (Recommended for Initial Testing)

#### Step 1: Test the Sync Workflow (`silabs-update-branches.yaml`)

1. **Navigate to GitHub Actions:**
   - Go to: `https://github.com/SiliconLabsSoftware/matter_sdk/actions`
   - Or: Repository → Actions tab

2. **Trigger the workflow manually:**
   - Find "Daily Sync of CSA branches" in the workflow list
   - Click on it, then click "Run workflow"
   - Select branch: `main` (required - workflow only runs on main)
   - Click "Run workflow"

3. **Verify the workflow runs:**
   - You should see 2 jobs running in parallel (one for `csa`, one for `csa-v1.5-branch`)
   - Check that both jobs complete successfully
   - Verify logs show:
     - ✅ Checkout of correct branches
     - ✅ Adding upstream remote
     - ✅ Pulling from upstream (master/v1.5-branch)
     - ✅ Pushing to origin (csa/csa-v1.5-branch)

4. **Check the branches:**
   - Verify branches were updated: `git fetch && git log origin/csa -1` and `git log origin/csa-v1.5-branch -1`

#### Step 2: Test the PR Workflow (`silabs-open-csa-prs.yaml`)

**Method A: Automatic Trigger (After Sync Workflow)**
- After Step 1 completes successfully, the PR workflow should automatically trigger
- Check Actions tab for "Open PRs from CSA branches"
- Verify it creates PRs for both branches

**Method B: Manual Trigger (For Testing PR Workflow Independently)**
1. Go to Actions → "Open PRs from CSA branches"
2. Click "Run workflow"
3. Select branch: `main`
4. Click "Run workflow"
5. Verify:
   - 2 jobs run in parallel
   - PRs are created:
     - `automation/update_main` → `main` (from `csa` branch)
     - `automation/update_release_2.8-1.5` → `release_2.8-1.5` (from `csa-v1.5-branch`)

### Option 2: Testing via Pull Request (Safer for Production)

1. **Create a test branch:**
   ```bash
   git checkout -b test/consolidated-workflows
   ```

2. **Commit the new workflow files:**
   ```bash
   git add .github/workflows/silabs-update-branches.yaml
   git add .github/workflows/silabs-open-csa-prs.yaml
   git commit -m "Consolidate CSA sync and PR workflows"
   git push origin test/consolidated-workflows
   ```

3. **Create a PR and test:**
   - The workflows won't run automatically on non-main branches (due to `if: github.ref == 'refs/heads/main'`)
   - But you can verify the YAML syntax is correct
   - Review the workflow files in the PR

4. **After merging to main:**
   - The workflows will be active
   - Test manually via GitHub UI as described in Option 1

### Option 3: Dry Run Testing (Limited)

You can use `act` (local GitHub Actions runner) for basic syntax testing:
```bash
# Install act: https://github.com/nektos/act
act workflow_dispatch -W .github/workflows/silabs-update-branches.yaml
```

**Note:** This won't work fully due to secrets and GitHub App tokens, but can catch YAML syntax errors.

## What to Verify

### Sync Workflow Success Criteria:
- ✅ Both matrix jobs complete successfully
- ✅ Branches are updated with latest from upstream
- ✅ No merge conflicts
- ✅ Git operations complete without errors

### PR Workflow Success Criteria:
- ✅ Both PRs are created successfully
- ✅ PR titles and bodies are correct
- ✅ PRs target correct base branches
- ✅ Labels are applied correctly
- ✅ PRs are created from correct source branches

## Troubleshooting

### Issue: Workflow doesn't trigger
- **Check:** Are you on the `main` branch? (workflow has `if: github.ref == 'refs/heads/main'`)
- **Solution:** Ensure workflow is triggered from `main` branch

### Issue: PR workflow doesn't trigger after sync
- **Check:** Did sync workflow complete successfully?
- **Check:** Is the workflow name exactly "Daily Sync of CSA branches"?
- **Solution:** Verify workflow names match exactly

### Issue: Matrix jobs fail
- **Check:** Are secrets/variables configured correctly?
- **Check:** Do you have permissions to push to the branches?
- **Solution:** Verify GitHub App has correct permissions

### Issue: PRs not created
- **Check:** Are there actual changes to sync?
- **Check:** Do the branches exist?
- **Solution:** The `create-pull-request` action will skip if there are no changes (this is expected behavior)

## Rollback Plan

If something goes wrong:
1. Keep the old workflow files until testing is complete
2. If issues occur, you can:
   - Revert the PR that added consolidated workflows
   - Or manually trigger the old workflows if they still exist
   - Or restore the old workflow files from git history

## Next Steps After Successful Testing

1. ✅ Verify both workflows work correctly
2. ✅ Confirm PRs are created with correct content
3. ✅ Delete old workflow files:
   - `silabs-update-v1-5-branch.yaml`
   - `silabs-update-csa.yaml`
   - `silabs-open-csa-pr.yaml`
   - `silabs-open-csa-1-5-branch-pr.yaml`
4. ✅ Monitor the workflows for a few days to ensure they run on schedule

