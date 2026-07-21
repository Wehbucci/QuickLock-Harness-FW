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

/* Mirrors config.h defaults, kept local so the test has no config dependency. */
static proximity_config_t default_cfg(void)
{
    proximity_config_t cfg = {
        .c_dbm = -60.0f,
        .n = 2.0f,
        .alpha = 0.15f,
        .out_threshold_dbm = -74.0f,
        .in_threshold_dbm = -70.0f,
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
    proximity_evaluate(&p);

    /* One deep dropout well past the out threshold. */
    proximity_update(&p, -95);
    proximity_event_t ev = proximity_evaluate(&p);

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
    proximity_evaluate(&p);

    int out_edges = 0;
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -85);          /* walk away and stay away */
        if (proximity_evaluate(&p) == PROX_WENT_OUT) {
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
    proximity_evaluate(&p);
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -85);
        proximity_evaluate(&p);
    }
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));

    /* Hold the filtered value inside the dead band (-74 .. -70): a level of -72
     * sits between the thresholds, so it must NOT come back in range. */
    int in_edges = 0;
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -72);
        if (proximity_evaluate(&p) == PROX_CAME_IN) {
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
    proximity_evaluate(&p);
    for (int i = 0; i < 100; i++) { proximity_update(&p, -85); proximity_evaluate(&p); }
    TEST_ASSERT_TRUE(proximity_is_out_of_range(&p));

    int in_edges = 0;
    for (int i = 0; i < 100; i++) {
        proximity_update(&p, -55);          /* strong signal, clearly in range */
        if (proximity_evaluate(&p) == PROX_CAME_IN) {
            in_edges++;
        }
    }
    TEST_ASSERT_EQUAL(1, in_edges);
    TEST_ASSERT_FALSE(proximity_is_out_of_range(&p));
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
