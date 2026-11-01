#include "backprop_engine.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace workbench {

ForwardPassResult BackpropEngine::forward(double x, double y, const BackpropWeights& w) {
    ForwardPassResult res;
    res.x = x;
    res.y = y;
    res.z = w.w * x + w.b;
    res.h = std::tanh(res.z);
    res.y_hat = w.v * res.h + w.c;
    double diff = res.y_hat - y;
    res.loss_J = 0.5 * diff * diff;
    return res;
}

BackpropGradients BackpropEngine::backward(const ForwardPassResult& f, const BackpropWeights& w) {
    BackpropGradients g;
    // dJ/dy_hat = (y_hat - y)
    double dJ_dyhat = f.y_hat - f.y;

    // Output layer: y_hat = v * h + c
    // dJ/dc = dJ/dy_hat * dy_hat/dc = (y_hat - y) * 1.0
    g.dJ_dc = dJ_dyhat;

    // dJ/dv = dJ/dy_hat * dy_hat/dv = (y_hat - y) * h
    g.dJ_dv = dJ_dyhat * f.h;

    // Backprop into hidden activation:
    // dJ/dh = dJ/dy_hat * dy_hat/dh = (y_hat - y) * v
    double dJ_dh = dJ_dyhat * w.v;

    // Hidden layer: h = tanh(z), dh/dz = 1 - tanh^2(z) = 1 - h^2
    // dJ/dz = dJ/dh * dh/dz = (y_hat - y) * v * (1 - h^2)
    double dJ_dz = dJ_dh * (1.0 - f.h * f.h);

    // Input layer: z = w * x + b
    // dJ/dw = dJ/dz * dz/dw = dJ/dz * x
    g.dJ_dw = dJ_dz * f.x;

    // dJ/db = dJ/dz * dz/db = dJ/dz * 1.0
    g.dJ_db = dJ_dz;

    return g;
}

BackpropWeights BackpropEngine::update(const BackpropWeights& w, const BackpropGradients& g, double eta) {
    BackpropWeights updated;
    // Simultaneous parameter update: theta <- theta - eta * dJ/dtheta
    updated.w = w.w - eta * g.dJ_dw;
    updated.b = w.b - eta * g.dJ_db;
    updated.v = w.v - eta * g.dJ_dv;
    updated.c = w.c - eta * g.dJ_dc;
    return updated;
}

double BackpropEngine::compute_numerical_gradient(double x, double y, const BackpropWeights& w,
                                                  const std::string& param_name, double eps) {
    BackpropWeights w_plus = w;
    BackpropWeights w_minus = w;

    if (param_name == "w") {
        w_plus.w += eps;
        w_minus.w -= eps;
    } else if (param_name == "b") {
        w_plus.b += eps;
        w_minus.b -= eps;
    } else if (param_name == "v") {
        w_plus.v += eps;
        w_minus.v -= eps;
    } else if (param_name == "c") {
        w_plus.c += eps;
        w_minus.c -= eps;
    }

    ForwardPassResult f_plus = forward(x, y, w_plus);
    ForwardPassResult f_minus = forward(x, y, w_minus);

    // Central difference formula: [J(theta + eps) - J(theta - eps)] / (2 * eps)
    return (f_plus.loss_J - f_minus.loss_J) / (2.0 * eps);
}

BackpropSummary BackpropEngine::run_full_study() {
    BackpropSummary summary;
    summary.input_x = 1.0;
    summary.target_y = 2.0;
    summary.learning_rate_eta = 0.1;
    summary.finite_diff_epsilon = 1e-6;

    // Initial weights
    BackpropWeights current_w;
    current_w.w = 0.5;
    current_w.b = 0.0;
    current_w.v = 1.0;
    current_w.c = 0.0;

    // Step 0 evaluation
    ForwardPassResult f0 = forward(summary.input_x, summary.target_y, current_w);
    BackpropGradients g0 = backward(f0, current_w);

    summary.step_0.step = 0;
    summary.step_0.weights = current_w;
    summary.step_0.forward = f0;
    summary.step_0.gradients = g0;

    // Update weights for step 1
    summary.updated_weights_step_1 = update(current_w, g0, summary.learning_rate_eta);

    // Finite difference gradient checks for all 4 parameters
    const double eps = summary.finite_diff_epsilon;
    const double tolerance = 1e-5;
    summary.all_gradient_checks_passed = true;

    auto check_param = [&](const std::string& name, double analytical_val) {
        GradientCheckResult check;
        check.param_name = name;
        check.analytical_grad = analytical_val;
        check.numerical_grad = compute_numerical_gradient(summary.input_x, summary.target_y, current_w, name, eps);
        check.abs_difference = std::abs(check.analytical_grad - check.numerical_grad);
        check.passed = (check.abs_difference <= tolerance);
        if (!check.passed) summary.all_gradient_checks_passed = false;
        summary.gradient_checks.push_back(check);
    };

    check_param("w", g0.dJ_dw);
    check_param("b", g0.dJ_db);
    check_param("v", g0.dJ_dv);
    check_param("c", g0.dJ_dc);

    // 100 Training steps
    summary.history_100_steps.clear();
    summary.history_100_steps.reserve(101);

    // Record initial state
    summary.history_100_steps.push_back(summary.step_0);

    for (size_t step = 1; step <= 100; ++step) {
        current_w = update(current_w, g0, summary.learning_rate_eta);
        ForwardPassResult f = forward(summary.input_x, summary.target_y, current_w);
        g0 = backward(f, current_w);

        TrainingStepRecord rec;
        rec.step = step;
        rec.weights = current_w;
        rec.forward = f;
        rec.gradients = g0;
        summary.history_100_steps.push_back(rec);
    }

    return summary;
}

std::string BackpropSummary::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << "{";
    ss << "\"input_x\":" << input_x << ",";
    ss << "\"target_y\":" << target_y << ",";
    ss << "\"learning_rate\":" << learning_rate_eta << ",";
    ss << "\"epsilon\":" << finite_diff_epsilon << ",";
    ss << "\"all_checks_passed\":" << (all_gradient_checks_passed ? "true" : "false") << ",";

    // Step 0 forward
    ss << "\"step_0\":{";
    ss << "\"w\":" << step_0.weights.w << ",";
    ss << "\"b\":" << step_0.weights.b << ",";
    ss << "\"v\":" << step_0.weights.v << ",";
    ss << "\"c\":" << step_0.weights.c << ",";
    ss << "\"z\":" << step_0.forward.z << ",";
    ss << "\"h\":" << step_0.forward.h << ",";
    ss << "\"y_hat\":" << step_0.forward.y_hat << ",";
    ss << "\"loss_J\":" << step_0.forward.loss_J << ",";
    ss << "\"dJ_dw\":" << step_0.gradients.dJ_dw << ",";
    ss << "\"dJ_db\":" << step_0.gradients.dJ_db << ",";
    ss << "\"dJ_dv\":" << step_0.gradients.dJ_dv << ",";
    ss << "\"dJ_dc\":" << step_0.gradients.dJ_dc;
    ss << "},";

    // Updated step 1
    ss << "\"updated_weights_step_1\":{";
    ss << "\"w\":" << updated_weights_step_1.w << ",";
    ss << "\"b\":" << updated_weights_step_1.b << ",";
    ss << "\"v\":" << updated_weights_step_1.v << ",";
    ss << "\"c\":" << updated_weights_step_1.c;
    ss << "},";

    // Checks
    ss << "\"gradient_checks\":[";
    for (size_t i = 0; i < gradient_checks.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& c = gradient_checks[i];
        ss << "{\"param\":\"" << c.param_name << "\","
           << "\"analytical\":" << c.analytical_grad << ","
           << "\"numerical\":" << c.numerical_grad << ","
           << "\"abs_diff\":" << std::scientific << std::setprecision(10) << c.abs_difference << ","
           << std::fixed << std::setprecision(8)
           << "\"passed\":" << (c.passed ? "true" : "false") << "}";
    }
    ss << "],";

    // 100 steps
    ss << "\"training_steps\":[";
    for (size_t i = 0; i < history_100_steps.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& h = history_100_steps[i];
        ss << "{\"step\":" << h.step << ","
           << "\"w\":" << h.weights.w << ","
           << "\"b\":" << h.weights.b << ","
           << "\"v\":" << h.weights.v << ","
           << "\"c\":" << h.weights.c << ","
           << "\"y_hat\":" << h.forward.y_hat << ","
           << "\"loss_J\":" << h.forward.loss_J << "}";
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

std::string BackpropSummary::to_csv() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << "# Simple Handwritten Backpropagation Training History (100 Steps)\n";
    ss << "# Architecture: h = tanh(w*x + b), y_hat = v*h + c, J = 0.5*(y_hat - y)^2\n";
    ss << "# Sample: x=" << input_x << ", y=" << target_y << ", Learning Rate eta=" << learning_rate_eta << "\n";
    ss << "# Initial: w=0.5, b=0, v=1, c=0\n";
    ss << "# Finite Difference Verification (epsilon=" << finite_diff_epsilon << ", threshold <= 1e-5):\n";
    for (const auto& c : gradient_checks) {
        ss << "#   Param " << c.param_name << ": Analytical=" << c.analytical_grad 
           << " | Numerical=" << c.numerical_grad << " | Diff=" << c.abs_difference 
           << " (" << (c.passed ? "PASSED" : "FAILED") << ")\n";
    }
    ss << "step,w,b,v,c,z,h,y_hat,loss_J,dJ_dw,dJ_db,dJ_dv,dJ_dc\n";

    for (const auto& h : history_100_steps) {
        ss << h.step << "," << h.weights.w << "," << h.weights.b << "," 
           << h.weights.v << "," << h.weights.c << "," << h.forward.z << "," 
           << h.forward.h << "," << h.forward.y_hat << "," << h.forward.loss_J << "," 
           << h.gradients.dJ_dw << "," << h.gradients.dJ_db << "," 
           << h.gradients.dJ_dv << "," << h.gradients.dJ_dc << "\n";
    }

    return ss.str();
}

} // namespace workbench
