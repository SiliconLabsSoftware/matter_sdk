#ifndef TEST_SAMPLE_H
#define TEST_SAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file test_sample.h
 * @brief Header file for test sample functions
 */

/**
 * @brief Calculate the sum of two integers
 * @param a First integer
 * @param b Second integer
 * @return Sum of a and b
 */
int add(int a, int b);

/**
 * @brief Calculate the factorial of a number
 * @param n The number to calculate factorial for
 * @return Factorial of n
 */
long factorial(int n);

/**
 * @brief Structure to represent a point in 2D space
 */
typedef struct {
    double x;  /**< X coordinate */
    double y;  /**< Y coordinate */
} Point;

/**
 * @brief Calculate distance between two points
 * @param p1 First point
 * @param p2 Second point
 * @return Distance between the points
 */
double calculateDistance(const Point* p1, const Point* p2);

/**
 * @brief Enum for operation types
 */
typedef enum {
    OP_ADD = 0,     /**< Addition operation */
    OP_SUBTRACT,    /**< Subtraction operation */
    OP_MULTIPLY,    /**< Multiplication operation */
    OP_DIVIDE       /**< Division operation */
} OperationType;

#ifdef __cplusplus
}
#endif

#endif // TEST_SAMPLE_H
