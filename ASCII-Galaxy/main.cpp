//
//  main.cpp
//  ASCII-Galaxy
//
//  3D Spiral Galaxy Animation: ASCII, True Color ANSI
//  By Brian

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/ioctl.h>

// Screen size (auto-detected)
static int W = 120;
static int H = 40;

// Galaxy parameters
static const int   ARMS    = 2;
static const float WIND    = 2.8f;
static const float ARM_W   = 0.30f;
static const float GAL_R   = 2.2f;
static const float BULGE_S = 0.22f;
static const float DISK_S  = 0.85f;
static const float TILT    = 1.05f;
static const float CAM_D   = 5.0f;
static const float FOV_S   = 0.30f;
static const int   N_STARS = 8000;
static const int   N_BG    = 100;

// Luminance ramp (dim → bright)
static const char* LUM  = " .,:;=+*#%@";
static int         LUMN;

struct GalaxyPoint {
    float x, y, z;
    float brightness;
    float cr, cg, cb;
};

struct BackgroundStar {
    int x, y;
    float brightness, phase;
};

// Screen buffers (brightness + RGB accumulators)
static float* buf_b   = nullptr;
static float* buf_r   = nullptr;
static float* buf_g   = nullptr;
static float* buf_bl  = nullptr;
static char*  outbuf  = nullptr;

static GalaxyPoint*   gpts   = nullptr;
static int            ngpts  = 0;
static BackgroundStar bgstars[200];

static volatile bool running = true;
static volatile bool resized = true;
static void on_signal(int) { running = false; }
static void on_resize(int) { resized = true; }

static inline float frand()    { return (float)rand() / RAND_MAX; }
static inline float gaussian() {
    return sqrtf(-2.f * logf(frand() + 1e-7f)) * cosf(6.28318f * frand());
}

// Logarithmic spiral arm density at polar (r, phi)
static float spiral_arm_density(float r, float phi) {
    float total = 0;
    for (int a = 0; a < ARMS; a++) {
        float ang = phi - WIND * logf(r + 0.08f)
                        - a * (6.28318f / ARMS);
        ang = fmodf(ang + 628.318f, 6.28318f);
        if (ang > 3.14159f) ang -= 6.28318f;
        float d = fabsf(ang) * (r + 0.02f);
        float w = ARM_W * (0.35f + 0.65f * sqrtf(r + 0.01f));
        total += expf(-d * d / (2.f * w * w));
    }
    return total;
}

// Dust lanes on inner edge of spiral arms
static float dust_absorption(float r, float phi) {
    if (r < 0.15f || r > 1.9f) return 0;
    float total = 0;
    for (int a = 0; a < ARMS; a++) {
        float ang = phi - WIND * logf(r + 0.08f)
                        - a * (6.28318f / ARMS);
        ang = fmodf(ang + 628.318f, 6.28318f);
        if (ang > 3.14159f) ang -= 6.28318f;
        float d = ang * (r + 0.02f);
        float w = ARM_W * 0.22f * sqrtf(r);
        float center = w * 0.45f;
        total += 0.35f * expf(-(d - center) * (d - center)
                               / (w * w * 0.20f));
    }
    return total < 0.55f ? total : 0.55f;
}

// Color blend: bulge=gold, arms=blue, HII=pink
static void galaxy_color(float r, float ad, bool hii,
                          float& cr, float& cg, float& cb) {
    float bf = expf(-r * r / (2.f * BULGE_S * BULGE_S));
    float af = ad * (1.f - bf);
    float df = (1.f - bf) * (1.f - af) * 0.3f;

    cr = bf * 1.00f + af * 0.50f + df * 0.70f;
    cg = bf * 0.87f + af * 0.58f + df * 0.65f;
    cb = bf * 0.55f + af * 1.00f + df * 0.60f;

    if (hii) {
        cr = cr * 0.30f + 1.00f * 0.70f;
        cg = cg * 0.30f + 0.45f * 0.70f;
        cb = cb * 0.30f + 0.70f * 0.70f;
    }
}

// Build galaxy model: density field + bulge halo + star particles
static void build_galaxy() {
    // Continuous density field (thin disk)
    for (float r = 0.01f; r < GAL_R; r += 0.007f) {
        float dphi = 0.022f / (r + 0.03f);
        if (dphi < 0.012f) dphi = 0.012f;
        if (dphi > 0.20f)  dphi = 0.20f;

        for (float phi = 0; phi < 6.28318f; phi += dphi) {
            float ad = spiral_arm_density(r, phi);
            float bg = expf(-r * r / (2.f * BULGE_S * BULGE_S));
            float dk = expf(-r / DISK_S) * 0.06f;
            float du = dust_absorption(r, phi);

            float b = (ad * 0.32f + bg * 0.72f + dk) * (1.f - du);
            b *= (0.82f + 0.18f * sinf(phi * 11.f + r * 7.3f));
            if (b < 0.01f) continue;

            GalaxyPoint& p = gpts[ngpts++];
            p.x = r * cosf(phi);
            p.y = r * sinf(phi);
            p.z = 0;
            p.brightness = b;
            galaxy_color(r, ad, false, p.cr, p.cg, p.cb);
        }
    }

    // Central bulge (3D ellipsoid)
    for (int i = 0; i < 3000; i++) {
        float r   = fabsf(gaussian()) * BULGE_S * 1.6f;
        if (r > BULGE_S * 5) continue;
        float phi = frand() * 6.28318f;
        float z   = gaussian() * BULGE_S * 0.7f;
        float b   = 0.45f * expf(-(r * r + z * z * 2.f)
                                   / (2.f * BULGE_S * BULGE_S));
        if (b < 0.01f) continue;

        GalaxyPoint& p = gpts[ngpts++];
        p.x = r * cosf(phi);
        p.y = r * sinf(phi);
        p.z = z;
        p.brightness = b;
        p.cr = 1.0f; p.cg = 0.90f; p.cb = 0.60f;
    }

    // Star particles (rejection-sampled into arms + bulge)
    int attempts = 0;
    for (int i = 0; i < N_STARS && attempts < N_STARS * 8; attempts++) {
        float r   = -DISK_S * logf(1.f - frand() * 0.96f);
        if (r > GAL_R) continue;
        float phi = frand() * 6.28318f;
        float ad  = spiral_arm_density(r, phi);
        float bg  = expf(-r * r / (2.f * BULGE_S * BULGE_S));

        if (frand() > (ad * 0.50f + bg * 0.40f + 0.08f)) continue;
        i++;

        bool hii = (ad > 0.30f && frand() < 0.09f && r > 0.18f);

        GalaxyPoint& p = gpts[ngpts++];
        p.x = r * cosf(phi);
        p.y = r * sinf(phi);
        p.z = gaussian() * (0.005f + 0.11f * bg);
        p.brightness = (0.10f + 0.90f * frand())
                      * (0.18f + ad * 0.50f + bg * 0.55f);
        if (hii) p.brightness *= 2.5f;
        galaxy_color(r, ad, hii, p.cr, p.cg, p.cb);
    }
}

static int   sz = 0;
static float K1 = 0;

// Allocate screen buffers and compute projection scale K1
static void alloc_buffers() {
    delete[] buf_b;  delete[] buf_r;
    delete[] buf_g;  delete[] buf_bl;
    delete[] outbuf;
    sz     = W * H;
    buf_b  = new float[sz];
    buf_r  = new float[sz];
    buf_g  = new float[sz];
    buf_bl = new float[sz];
    outbuf = new char[sz * 28 + 4096];

    // K1 = projection scale, fitted so galaxy fills the screen
    float sT     = sinf(TILT), cT = cosf(TILT);
    float z_near = CAM_D - GAL_R * sT;
    float K1_x   = (W * 0.5f * z_near) / GAL_R;
    float K1_y   = (H * 0.5f * z_near) / (GAL_R * cT * 0.5f);
    K1 = (K1_x < K1_y ? K1_x : K1_y);

    for (int i = 0; i < N_BG; i++) {
        bgstars[i] = {
            rand() % W, rand() % H,
            0.04f + frand() * 0.16f,
            frand() * 6.28318f
        };
    }
}

static void detect_and_resize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 30) {
        W = ws.ws_col;
        H = ws.ws_row - 1;
        if (H < 10) H = 10;
    }
    alloc_buffers();
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
}

int main(int argc, const char* argv[]) {
    LUMN = (int)strlen(LUM);
    srand(42);

    int max_frames  = 0;
    int forced_cols = 0;
    int forced_rows = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cols") && i + 1 < argc)
            forced_cols = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc)
            forced_rows = atoi(argv[++i]);
    }

    signal(SIGINT,   on_signal);
    signal(SIGTERM,  on_signal);
    signal(SIGWINCH, on_resize);

    detect_and_resize();

    if (forced_cols > 0 || forced_rows > 0) {
        if (forced_cols > 0) W = forced_cols;
        if (forced_rows > 0) H = forced_rows;
        alloc_buffers();
    }

    gpts = new GalaxyPoint[350000];
    build_galaxy();

    printf("\x1b[?25l"); // hide cursor
    fflush(stdout);

    float A  = 0;  // rotation angle
    float cT = cosf(TILT), sT = sinf(TILT);
    int   frame = 0;

    // Render loop
    while (running && (max_frames == 0 || frame < max_frames)) {
        if (max_frames == 0 && resized) {
            resized = false;
            detect_and_resize();
        }

        memset(buf_b,  0, sz * sizeof(float));
        memset(buf_r,  0, sz * sizeof(float));
        memset(buf_g,  0, sz * sizeof(float));
        memset(buf_bl, 0, sz * sizeof(float));

        float cA = cosf(A), sA = sinf(A);

        // Background stars
        for (int i = 0; i < N_BG; i++) {
            float b = bgstars[i].brightness
                    * (0.45f + 0.55f * sinf(A * 5.f + bgstars[i].phase));
            if (b < 0.005f) continue;
            int idx = bgstars[i].y * W + bgstars[i].x;
            buf_b[idx]  += b;
            buf_r[idx]  += b * 0.85f;
            buf_g[idx]  += b * 0.85f;
            buf_bl[idx] += b * 1.0f;
        }

        //spin(z-axis) → tilt(x-axis) → perspective
        for (int i = 0; i < ngpts; i++) {
            const GalaxyPoint& gp = gpts[i];

            float sx = gp.x * cA - gp.y * sA;
            float sy = gp.x * sA + gp.y * cA;
            float sz = gp.z;

            float ty = sy * cT - sz * sT;
            float tz = sy * sT + sz * cT;

            float z = tz + CAM_D;
            if (z < 0.1f) continue;
            float ooz = 1.f / z;

            int xp = (int)(W * 0.5f + K1 * ooz * sx);
            int yp = (int)(H * 0.5f - K1 * ooz * ty * 0.5f);

            if (xp < 0 || xp >= W || yp < 0 || yp >= H) continue;

            float c = gp.brightness * ooz * 2.2f;
            int idx = yp * W + xp;
            buf_b[idx]  += c;
            buf_r[idx]  += c * gp.cr;
            buf_g[idx]  += c * gp.cg;
            buf_bl[idx] += c * gp.cb;
        }

        // ANSI true-color output
        char* p = outbuf;
        int remain = sz * 28 + 4000;
        p += snprintf(p, remain, "\x1b[H");

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int idx = y * W + x;
                float b = buf_b[idx];

                if (b < 0.01f) {
                    *p++ = ' ';
                } else {
                    // Brightness → ASCII char (sqrt gamma)
                    int li = (int)(sqrtf(b) * (LUMN - 1) * 1.15f);
                    if (li >= LUMN) li = LUMN - 1;

                    // Normalize
                    float cr = buf_r[idx];
                    float cg = buf_g[idx];
                    float cb = buf_bl[idx];
                    float mx = cr > cg ? (cr > cb ? cr : cb)
                                       : (cg > cb ? cg : cb);
                    if (mx < 1e-6f) mx = 1e-6f;
                    cr /= mx; cg /= mx; cb /= mx;

                    float I = sqrtf(b < 1.f ? b : 1.f);
                    int ir = (int)(cr * I * 255);
                    int ig = (int)(cg * I * 255);
                    int ib = (int)(cb * I * 255);
                    if (ir > 255) ir = 255; if (ir < 0) ir = 0;
                    if (ig > 255) ig = 255; if (ig < 0) ig = 0;
                    if (ib > 255) ib = 255; if (ib < 0) ib = 0;

                    p += snprintf(p, 30, "\x1b[38;2;%d;%d;%dm%c",
                                  ir, ig, ib, LUM[li]);
                }
            }
            p += snprintf(p, 10, "\x1b[0m\n");
        }
        *p = '\0';

        fputs(outbuf, stdout);
        fflush(stdout);

        A += 0.012f;
        frame++;
        if (max_frames == 0) usleep(33000);
    }

    printf("\x1b[0m\x1b[?25h\n"); // restore cursor
    delete[] buf_b;  delete[] buf_r;
    delete[] buf_g;  delete[] buf_bl;
    delete[] outbuf; delete[] gpts;

    return 0;
}
