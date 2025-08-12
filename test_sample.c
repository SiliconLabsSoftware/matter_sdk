#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Calculate the sum of two integers
 * @param a First integer
 * @param b Second integer
 * @return Sum of a and b
 */
int add(int a, int b) {
    return a + b;
}

/**
 * @brief Calculate the difference of two integers
 * @param a First integer
 * @param b Second integer
 * @return Difference of a and b
 */
int subtract(int a, int b) {
    return a - b;
}

/**
 * @brief Calculate the factorial of a number
 * @param n The number to calculate factorial for
 * @return Factorial of n
 */
long factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

/**
 * @brief Main function to demonstrate the functions
 */
int main() {
    int x = 5, y = 3;
    printf("Sum of %d and %d is: %d\n", x, y, add(x, y));
    printf("Difference of %d and %d is: %d\n", x, y, subtract(x, y));
    
    int fact_num = 5;
    printf("Factorial of %d is: %ld\n", fact_num, factorial(fact_num));
    
    return 0;
}
