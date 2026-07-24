#include "imu_tilt_filter.h"

#include <math.h>
#include <string.h>

#define IMU_SAMPLE_DT_S              0.01f   /* 100 Hz design point (T1) */
#define TILT_FILTER_ACCEL_GAIN       0.02f   /* accel blend; tau = dt/gain = 0.5 s */
#define TILT_ACCEL_TRUST_BAND_G      0.15f   /* trust accel only when | ||a||-1g | is below this */
#define TILT_BASELINE_SETTLE_SAMPLES 100     /* 1 s of TRUSTED samples before the reference locks */
#define DEG_TO_RAD                   0.017453293f
#define RAD_TO_DEG                   57.29578f

static inline float vec3_dot(imu_vec3_t a, imu_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline imu_vec3_t vec3_cross(imu_vec3_t a, imu_vec3_t b)
{
    imu_vec3_t r = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
    return r;
}

/* Normalizes in place; false if the norm is too small to define a direction
 * (freefall-ish accel sample, or a degenerate estimate). */
static bool vec3_normalize(imu_vec3_t *v)
{
    float norm = sqrtf(vec3_dot(*v, *v));
    if (norm < 1e-3f) {
        return false;
    }
    v->x /= norm;
    v->y /= norm;
    v->z /= norm;
    return true;
}

void imu_tilt_filter_init(imu_tilt_filter_t *f)
{
    memset(f, 0, sizeof(*f));
}

void imu_tilt_filter_reset_estimate(imu_tilt_filter_t *f)
{
    f->g_est_valid = false;
    f->g_ref_valid = false;
    f->settle_samples = 0;
}

void imu_tilt_filter_reset_reference(imu_tilt_filter_t *f)
{
    f->g_ref_valid = false;
    f->settle_samples = 0;
}

void imu_tilt_filter_update(imu_tilt_filter_t *f, const imu_data_t *sample)
{
    imu_vec3_t a_vec = { sample->accel_x_g, sample->accel_y_g, sample->accel_z_g };
    float accel_norm = sqrtf(vec3_dot(a_vec, a_vec));
    bool accel_trusted = (fabsf(accel_norm - 1.0f) < TILT_ACCEL_TRUST_BAND_G);

    if (!f->g_est_valid) {
        /* Seed from any sane accel sample (generous band: a slightly
         * moving unit still seeds; the filter converges from there). */
        if (accel_norm > 0.5f && accel_norm < 1.5f) {
            f->g_est = a_vec;
            f->g_est_valid = vec3_normalize(&f->g_est);
        }
    } else {
        /* Gyro propagation: gravity is fixed in the world, so in the body
         * frame it evolves as dg/dt = -omega x g = g x omega. First-order
         * integration is exact enough at 100 Hz and <500 dps; the
         * renormalization below removes the residual norm error. A missed
         * sample (data-ready timeout) under-integrates, which the accel
         * blend repairs within ~tau once quasi-static. */
        imu_vec3_t w_rad = { sample->gyro_x_dps * DEG_TO_RAD,
                             sample->gyro_y_dps * DEG_TO_RAD,
                             sample->gyro_z_dps * DEG_TO_RAD };
        imu_vec3_t dg = vec3_cross(f->g_est, w_rad);
        f->g_est.x += dg.x * IMU_SAMPLE_DT_S;
        f->g_est.y += dg.y * IMU_SAMPLE_DT_S;
        f->g_est.z += dg.z * IMU_SAMPLE_DT_S;

        if (accel_trusted) {
            imu_vec3_t a_hat = a_vec;
            if (vec3_normalize(&a_hat)) {
                f->g_est.x += TILT_FILTER_ACCEL_GAIN * (a_hat.x - f->g_est.x);
                f->g_est.y += TILT_FILTER_ACCEL_GAIN * (a_hat.y - f->g_est.y);
                f->g_est.z += TILT_FILTER_ACCEL_GAIN * (a_hat.z - f->g_est.z);
            }
        }
        f->g_est_valid = vec3_normalize(&f->g_est);
    }

    /* Reference locks only after a full second of TRUSTED (quasi-static,
     * blend-corrected) samples, so arming while the unit is still being
     * handled can't freeze a garbage attitude as the baseline. */
    if (f->g_est_valid && !f->g_ref_valid && accel_trusted) {
        if (++f->settle_samples >= TILT_BASELINE_SETTLE_SAMPLES) {
            f->g_ref = f->g_est;
            f->g_ref_valid = true;
        }
    }
}

bool imu_tilt_filter_reference_locked(const imu_tilt_filter_t *f)
{
    return f->g_ref_valid;
}

float imu_tilt_filter_get_tilt_deg(const imu_tilt_filter_t *f)
{
    if (!f->g_est_valid || !f->g_ref_valid) {
        return 0.0f;
    }
    return imu_tilt_angle_deg(f->g_est, f->g_ref);
}

bool imu_tilt_filter_direction_valid(const imu_tilt_filter_t *f)
{
    return f->g_est_valid;
}

imu_vec3_t imu_tilt_filter_get_direction(const imu_tilt_filter_t *f)
{
    return f->g_est;
}

float imu_tilt_angle_deg(imu_vec3_t a, imu_vec3_t b)
{
    float c = vec3_dot(a, b);
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return acosf(c) * RAD_TO_DEG;
}
