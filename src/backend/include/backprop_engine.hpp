#pragma once

#include <vector>
#include <string>

namespace workbench {

struct BackpropWeights {
    double w = 0.5;
    double b = 0.0;
    double v = 1.0;
    double c = 0.0;
};

struct BackpropGradients {
    double dJ_dw = 0.0;
    double dJ_db = 0.0;
    double dJ_dv = 0.0;
    double dJ_dc = 0.0;
};

struct ForwardPassResult {
    double x = 1.0;
    double y = 2.0;
    double z = 0.0;     // w*x + b
    double h = 0.0;     // tanh(z)
    double y_hat = 0.0; // v*h + c
    double loss_J = 0.0;// 0.5 * (y_hat - y)^2
};

struct GradientCheckResult {
    std::string param_name;
    double analytical_grad = 0.0;
    double numerical_grad = 0.0;
    double abs_difference = 0.0;
    bool passed = false; // difference <= 1e-5
};

struct TrainingStepRecord {
    size_t step = 0;
    BackpropWeights weights;
    ForwardPassResult forward;
    BackpropGradients gradients;
};

struct BackpropSummary {
    double input_x = 1.0;
    double target_y = 2.0;
    double learning_rate_eta = 0.1;
    double finite_diff_epsilon = 1e-6;

    // Step 0 forward and backward
    TrainingStepRecord step_0;
    BackpropWeights updated_weights_step_1;

    // Numerical gradient verification
    std::vector<GradientCheckResult> gradient_checks;
    bool all_gradient_checks_passed = false;

    // 100 steps training history
    std::vector<TrainingStepRecord> history_100_steps;

    std::string to_json() const;
    std::string to_csv() const;
};

class BackpropEngine {
public:
    static ForwardPassResult forward(double x, double y, const BackpropWeights& w);
    static BackpropGradients backward(const ForwardPassResult& f, const BackpropWeights& w);
    static BackpropWeights update(const BackpropWeights& w, const BackpropGradients& g, double eta);

    // Central finite difference verification: [J(theta + eps) - J(theta - eps)] / (2 * eps)
    static double compute_numerical_gradient(double x, double y, const BackpropWeights& w, 
                                             const std::string& param_name, double eps = 1e-6);

    // Full backprop run: Step 0, Gradient Check, 100 Training Steps
    static BackpropSummary run_full_study();
};

} // namespace workbench
