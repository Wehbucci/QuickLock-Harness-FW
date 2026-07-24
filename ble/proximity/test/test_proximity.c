/*
 * test_proximity.c — Unity tests for the pure proximity module.
 *
 * Because proximity.[ch] has no BLE/IDF/hardware dependency, these tests
 * exercise the model, the EMA filter, and the hysteresis in isolation. Run with
 * the IDF Unity test runner, e.g.:  idf.py -T proximity build flash monitor
 * (from a unity-test-app), or compile proximity.c + this file on a host.
 *
 * Serves: F14 (verifies the soft out-of-range decision logic).
 */

#include "unity.h"
#include "proximity.h"
#include <math.h>

/* Mirrors config.h defaults, kept local so the test has no config dependency.
 * out_confirm_ms = 0 here disables the out-of-range dwell, so the EMA- and
 * hysteresis-focused tests below see the legacy "declare on crossing" behaviour.
 * The dwell has its own dedicated tests further down. */
static proximity_config_t default_cfg(void)
{
    proximity_config_t cfg = {
        .c_dbm = -60.0f,
        .n = 2.0f,
        .alpha = 0.15f,
        .out_threshold_dbm = -74.0f,
        .in_threshold_dbm = -70.0f,
        .out_confirm_ms = 0,
    };
    return cfg;
}

TEST_CASE("first sample seeds the EMA exactly", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    float f = proximity_update(&p, -55);
    /* No easing from zero: the very first filtered value equals the raw sample. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -55.0f, f);
}

TEST_CASE("EMA converges toward a steady raw level", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    proximity_update(&p, -60);              /* seed */
    for (int i = 0; i < 200; i++) {
        proximity_update(&p, -80);          /* hold a new steady level */
    }
    /* With alpha=0.15 the filter should have essentially settled on -80. */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -80.0f, proximity_filtered_rssi(&p));
}

TEST_CASE("a single noise spike does not flip the decision", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    proximity_update(&p, -60);              /* seed comfortably in range */
    proximity_evaluate(&p, 0);

    /* One deep dropout well past the out threshold. */
    proximity_update(&p, -95);
    proximity_event_t ev = proximity_evaluate(&p, 0);

    /* alpha=0.15: filt = 0.85*-60 + 0.15*-95 = -65.25, still above -74. */
    TEST_ASSERT_EQUAL(PROX_NO_CHANGE, ev);
    TEST_ASSERT_FALSE(proximity_is_out_of_range(&p));
}

TEST_CASE("sustained weak signal trips out-of-range once", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    proximity_update(&p, -60);              /* seed in range */
    proximity_evaluate(&p, 0);

    int out_edges = 0;
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -85);          /* walk away and stay away */
        if (proximity_evaluate(&p, 0) == PROX_WENT_OUT) {
            out_edges++;
        }
    }
    TEST_ASSERT_EQUAL(1, out_edges);        /* edge reported exactly once */
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));
}

TEST_CASE("hysteresis dead band prevents chatter", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    /* Drive out of range. */
    proximity_update(&p, -60);
    proximity_evaluate(&p, 0);
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -85);
        proximity_evaluate(&p, 0);
    }
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));

    /* Hold the filtered value inside the dead band (-74 .. -70): a level of -72
     * sits between the thresholds, so it must NOT come back in range. */
    int in_edges = 0;
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -72);
        if (proximity_evaluate(&p, 0) == PROX_CAME_IN) {
            in_edges++;
        }
    }
    TEST_ASSERT_EQUAL(0, in_edges);
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));
}

TEST_CASE("crossing the in-threshold reports came-in once", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    proximity_update(&p, -60);
    proximity_evaluate(&p, 0);
    for (int i = 0; i < 100; i++) { proximity_update(&p, -85); proximity_evaluate(&p, 0); }
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));

    int in_edges = 0;
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -55);          /* strong signal, clearly in range */
        if (proximity_evaluate(&p, 0) == PROX_CAME_IN) {
            in_edges++;
        }
    }
    TEST_ASSERT_EQUAL(1, in_edges);
    TEST_ASSERT_FALSE(proximity_is_out_of_range(&p));
}

/* ---- Out-of-range dwell (F14 transient-misread rejection) ----
 * These use alpha = 1.0 so the filtered value equals the raw sample, which lets
 * the tests drive the filtered RSSI exactly and reason about the dwell in real
 * milliseconds. out_confirm_ms = 10 s, sampled at 1 s like the harness. */
static proximity_config_t dwell_cfg(void)
{
    proximity_config_t cfg = {
        .c_dbm = -60.0f,
        .n = 2.0f,
        .alpha = 1.0f,            /* filt == raw, for exact control */
        .out_threshold_dbm = -74.0f,
        .in_threshold_dbm = -70.0f,
        .out_confirm_ms = 10000,  /* must stay out this long to be declared out */
    };
    return cfg;
}

TEST_CASE("transient dip below OUT does not trip before the dwell", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = dwell_cfg();
    proximity_init(&p, &cfg);

    uint32_t t = 1000;
    proximity_update(&p, -60);                 /* seed in range */
    TEST_ASSERT_EQUAL(PROX_NO_CHANGE, proximity_evaluate(&p, t));

    /* Drop below OUT for 3 s (< 10 s dwell), then recover. This is the misread. */
    for (int i = 0; i < 3; i++) {
        t += 1000;
        proximity_update(&p, -85);
        TEST_ASSERT_EQUAL(PROX_NO_CHANGE, proximity_evaluate(&p, t)); /* still pending */
    }
    t += 1000;
    proximity_update(&p, -60);                 /* back in range: cancels the timer */
    TEST_ASSERT_EQUAL(PROX_NO_CHANGE, proximity_evaluate(&p, t));

    /* Even long afterwards, no out-of-range: the dip never lasted the full dwell. */
    for (int i = 0; i < 30; i++) {
        t += 1000;
        proximity_update(&p, -60);
        TEST_ASSERT_EQUAL(PROX_NO_CHANGE, proximity_evaluate(&p, t));
    }
    TEST_ASSERT_FALSE(proximity_is_out_of_range(&p));
}

TEST_CASE("sustained weak signal trips out-of-range after the dwell, once", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = dwell_cfg();
    proximity_init(&p, &cfg);

    uint32_t t = 1000;
    proximity_update(&p, -60);
    proximity_evaluate(&p, t);                 /* seed, in range */

    int out_edges = 0;
    uint32_t fired_at = 0;
    /* Hold below OUT from t=2000 onward. First dip is at t=2000, so the dwell
     * elapses at t=12000. */
    for (int i = 0; i < 30; i++) {
        t += 1000;
        proximity_update(&p, -85);
        if (proximity_evaluate(&p, t) == PROX_WENT_OUT) {
            out_edges++;
            fired_at = t;
        }
    }
    TEST_ASSERT_EQUAL(1, out_edges);           /* exactly one edge */
    TEST_ASSERT_EQUAL_UINT32(12000, fired_at); /* 10 s after the first dip at 2000 */
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));
}

TEST_CASE("a brief recovery restarts the dwell timer", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = dwell_cfg();
    proximity_init(&p, &cfg);

    uint32_t t = 1000;
    proximity_update(&p, -60);
    proximity_evaluate(&p, t);

    /* Below OUT for 8 s (t=2000..9000) -- not yet the full 10 s. */
    for (int i = 0; i < 8; i++) {
        t += 1000;
        proximity_update(&p, -85);
        TEST_ASSERT_EQUAL(PROX_NO_CHANGE, proximity_evaluate(&p, t));
    }
    /* One good sample recovers above OUT: the pending timer must reset. */
    t += 1000;
    proximity_update(&p, -60);
    proximity_evaluate(&p, t);

    /* Now drop again. If the timer had NOT reset, 8 s + 2 s would already trip;
     * it must not. It should only trip 10 s after THIS dip began. */
    uint32_t restart = 0;
    int out_edges = 0;
    for (int i = 0; i < 12; i++) {
        t += 1000;
        if (restart == 0) restart = t;         /* first dip of the second run */
        proximity_update(&p, -85);
        if (proximity_evaluate(&p, t) == PROX_WENT_OUT) {
            out_edges++;
            TEST_ASSERT_EQUAL_UINT32(restart + 10000, t);
        }
    }
    TEST_ASSERT_EQUAL(1, out_edges);
}

TEST_CASE("distance model matches the design anchor points", "[proximity]")
{
    proximity_t p;
    proximity_config_t cfg = default_cfg();
    proximity_init(&p, &cfg);

    /* At RSSI == C (-60) the distance is exactly d0 = 1 m. */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, proximity_distance_m(&p, -60.0f));
    /* Design doc: C=-60, n=2 -> ~5 m near -74 dBm. 10^((-60+74)/20)=10^0.7~5.01. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.01f, proximity_distance_m(&p, -74.0f));
}

TEST_CASE("invalid raw RSSI is rejected", "[proximity]")
{
    TEST_ASSERT_FALSE(proximity_rssi_is_valid(127)); /* NimBLE "unavailable" */
    TEST_ASSERT_FALSE(proximity_rssi_is_valid(0));   /* not a real link RSSI */
    TEST_ASSERT_TRUE(proximity_rssi_is_valid(-60));
}
