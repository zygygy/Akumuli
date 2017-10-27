#include "sliding_window.h"
#include "../storage_engine/operators/aggregate.h"

namespace Akumuli {
namespace QP {

// -------------
// SlidingWindow
// -------------

// ----
// EWMA
// ----

static const u32 EWMA_WARMUP = 10;

EWMA::EWMA()
    : warmup_(0)
    , value_(0.0)
    , decay_(0.0)
{
}

EWMA::EWMA(double decay)
    : warmup_(0)
    , value_(0.0)
    , last_(0)
    , decay_(decay)
{
}

void EWMA::add(double value) {
    if (warmup_ < EWMA_WARMUP) {
        value_ += value;
        warmup_++;
    } else if (warmup_ == EWMA_WARMUP) {
        warmup_++;
        value_ += value;
        value_ = value_ / static_cast<double>(warmup_);
        value_ = (value * decay_) + (value_ * (1.0 - decay_));
    } else {
        value_ = (value * decay_) + (value_ * (1.0 - decay_));
    }
}


double EWMA::get(double default_value) const {
    if (warmup_ <= EWMA_WARMUP) {
        return default_value;
    }
    return value_;
}

// -------------
// EWMAPrecition
// -------------

EWMAPrediction::EWMAPrediction(boost::property_tree::ptree const& ptree, std::shared_ptr<Node> next)
    : next_(next)
    , delta_(false)
{
    decay_ = ptree.get<double>("decay");
}

EWMAPrediction::EWMAPrediction(double decay, bool calculate_delta, std::shared_ptr<Node> next)
    : decay_(decay)
    , next_(next)
    , delta_(calculate_delta)
{
}

void EWMAPrediction::complete() {
    next_->complete();
}

bool EWMAPrediction::put(const aku_Sample &sample) {
    SampleUtil::Context ctx;
    double value;
    std::tie(value, ctx) = SampleUtil::get_value(sample);
    if (ctx == SampleUtil::ERROR) {
        return false;
    }
    // calculate next value
    if (swind_.count(sample.paramid) == 0) {
        swind_[sample.paramid] = EWMA(decay_);
    }
    EWMA& ewma = swind_[sample.paramid];
    double exp = ewma.get(value);
    ewma.add(value);
    if (delta_) {
        value = value - exp;
    } else {
        value = exp;
    }
    // publish next value
    return SampleUtil::publish(ctx, value, sample, next_.get());
}

void EWMAPrediction::set_error(aku_Status status) {
    next_->set_error(status);
}

int EWMAPrediction::get_requirements() const {
    return TERMINAL;
}

struct EWMAPredictionError : EWMAPrediction {
    EWMAPredictionError(boost::property_tree::ptree const& ptree, std::shared_ptr<Node> next)
        : EWMAPrediction(ptree.get<double>("decay"), true, next)
    {
    }
};

//! Register anomaly detector for use in queries
static QueryParserToken<EWMAPredictionError> ewma_error_token("ewma-error");
static QueryParserToken<EWMAPrediction> ewma_token("ewma");

}}  // namespace
