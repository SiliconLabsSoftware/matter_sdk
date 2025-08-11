#include <iostream>
#include <vector>
#include <string>
#include <memory>

/**
 * @class Calculator
 * @brief A simple calculator class with basic arithmetic operations
 */
class Calculator {
private:
    std::vector<double> history;

public:
    /**
     * @brief Add two numbers
     * @param a First number
     * @param b Second number
     * @return Sum of a and b
     */
    double add(double a, double b) {
        double result = a + b;
        history.push_back(result);
        return result;
    }

    /**
     * @brief Subtract two numbers
     * @param a First number
     * @param b Second number
     * @return Difference of a and b
     */
    double subtract(double a, double b) {
        double result = a - b;
        history.push_back(result);
        return result;
    }

    /**
     * @brief Multiply two numbers
     * @param a First number
     * @param b Second number
     * @return Product of a and b
     */
    double multiply(double a, double b) {
        double result = a * b;
        history.push_back(result);
        return result;
    }

    /**
     * @brief Divide two numbers
     * @param a Dividend
     * @param b Divisor
     * @return Quotient of a and b
     * @throws std::invalid_argument if divisor is zero
     */
    double divide(double a, double b) {
        if (b == 0) {
            throw std::invalid_argument("Division by zero is not allowed");
        }
        double result = a / b;
        history.push_back(result);
        return result;
    }

    /**
     * @brief Get the calculation history
     * @return Vector containing all previous results
     */
    const std::vector<double>& getHistory() const {
        return history;
    }

    /**
     * @brief Clear the calculation history
     */
    void clearHistory() {
        history.clear();
    }
};

/**
 * @brief Template function to find maximum of two values
 * @tparam T Type of the values
 * @param a First value
 * @param b Second value
 * @return Maximum of a and b
 */
template<typename T>
T findMax(T a, T b) {
    return (a > b) ? a : b;
}

/**
 * @brief Main function to demonstrate the Calculator class
 */
int main() {
    auto calculator = std::make_unique<Calculator>();
    
    std::cout << "Testing Calculator class:" << std::endl;
    
    try {
        double result1 = calculator->add(10.5, 5.3);
        std::cout << "10.5 + 5.3 = " << result1 << std::endl;
        
        double result2 = calculator->multiply(4.0, 3.0);
        std::cout << "4.0 * 3.0 = " << result2 << std::endl;
        
        double result3 = calculator->divide(15.0, 3.0);
        std::cout << "15.0 / 3.0 = " << result3 << std::endl;
        
        // Test template function
        int maxInt = findMax(42, 37);
        std::cout << "Max of 42 and 37: " << maxInt << std::endl;
        
        // Display history
        std::cout << "\nCalculation history:" << std::endl;
        const auto& history = calculator->getHistory();
        for (size_t i = 0; i < history.size(); ++i) {
            std::cout << "Result " << (i + 1) << ": " << history[i] << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
