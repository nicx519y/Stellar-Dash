#ifndef RF_HOP_SCORE_H
#define RF_HOP_SCORE_H

#include <stdint.h>

typedef struct {
    uint16_t base_score;
    uint16_t loss_weight;
    uint16_t crc_weight;
    uint16_t type_weight;
    uint16_t timeout_weight;
    uint16_t irq_weight;
} rfh_score_weights_t;

typedef struct {
    uint16_t loss_permille;
    uint16_t crc_permille;
    uint16_t type_permille;
    uint16_t timeout_permille;
    uint16_t irq_permille;
} rfh_score_metrics_t;

static inline uint16_t rfh_score_clamp(uint32_t value)
{
    return (value > 1000u) ? 1000u : (uint16_t)value;
}

static inline uint32_t rfh_score_weighted_term(uint16_t metric_permille,
                                               uint16_t weight_percent)
{
    return (((uint32_t)metric_permille * (uint32_t)weight_percent) + 50u) / 100u;
}

static inline uint16_t rfh_score_from_metrics(const rfh_score_metrics_t *metrics,
                                              const rfh_score_weights_t *weights)
{
    uint32_t score;

    if((metrics == 0) || (weights == 0))
    {
        return 1000u;
    }

    score = weights->base_score;
    score += rfh_score_weighted_term(metrics->loss_permille, weights->loss_weight);
    score += rfh_score_weighted_term(metrics->crc_permille, weights->crc_weight);
    score += rfh_score_weighted_term(metrics->type_permille, weights->type_weight);
    score += rfh_score_weighted_term(metrics->timeout_permille, weights->timeout_weight);
    score += rfh_score_weighted_term(metrics->irq_permille, weights->irq_weight);
    return rfh_score_clamp(score);
}

static inline uint16_t rfh_score_ema(uint16_t old_score,
                                     uint16_t sample_score,
                                     uint16_t old_weight,
                                     uint16_t new_weight)
{
    uint32_t total_weight = (uint32_t)old_weight + (uint32_t)new_weight;
    uint32_t score;

    if(total_weight == 0u)
    {
        return rfh_score_clamp(sample_score);
    }

    score = ((uint32_t)old_score * (uint32_t)old_weight) +
            ((uint32_t)sample_score * (uint32_t)new_weight) +
            (total_weight / 2u);
    score /= total_weight;
    return rfh_score_clamp(score);
}

static inline uint16_t rfh_score_irq_metric(uint16_t avg_irq_us,
                                            uint16_t warn_irq_us,
                                            uint16_t bad_irq_us)
{
    uint32_t metric;

    if(avg_irq_us <= warn_irq_us)
    {
        return 0u;
    }
    if((avg_irq_us >= bad_irq_us) || (bad_irq_us <= warn_irq_us))
    {
        return 1000u;
    }

    metric = ((uint32_t)(avg_irq_us - warn_irq_us) * 1000u) /
             (uint32_t)(bad_irq_us - warn_irq_us);
    return rfh_score_clamp(metric);
}

#endif
