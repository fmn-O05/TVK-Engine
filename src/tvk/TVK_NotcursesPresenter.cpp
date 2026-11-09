#include "tvk/TVK_NotcursesPresenter.h"

#ifdef TVK_NOTCURSES_AVAILABLE

#include <notcurses/notcurses.h>
#include <iostream>
#include <cstdlib>
#include <clocale>

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace tvk {

NotcursesPresenter::NotcursesPresenter(bool headless) : m_Headless(headless), m_NC(nullptr), m_StdPlane(nullptr), m_HudPlane(nullptr), m_TermW(0), m_TermH(0) {
    if (m_Headless) {
        return;
    }

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Explicitly set Windows UCRT locale to UTF-8
    if (!setlocale(LC_ALL, ".UTF8")) {
        // Explicitly set MinGW/MSYS locale to C.UTF-8 or fall back
        if (!setlocale(LC_ALL, "C.UTF-8")) {
            setlocale(LC_ALL, "en_US.UTF-8");
            setlocale(LC_ALL, ""); 
        }
    }
    // CRITICAL: Force stdout to binary mode so MSVCRT doesn't transcode 
    // our UTF-8 braille bytes (e.g. E2 A0 80) into CP1252 wide-chars (â£€) 
    // before sending to the Windows console.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    // CRITICAL: Since standard Windows/PowerShell does not set the TERM 
    // variable, Notcurses assumes a 'dumb' terminal and will just print 
    // linear scrolling text! We must force the VT100 terminfo capability 
    // so it uses cursor positioning and overwrites the screen like a video.
    _putenv("TERM=xterm-256color");
    _putenv("COLORTERM=truecolor");
    
    // Explicitly enable VT Sequence processing for the Windows Console out handle
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
        SetConsoleMode(hOut, dwMode);
    }
#else
    setlocale(LC_ALL, ""); // Linux/macOS
#endif

    notcurses_options opts = {};
    opts.flags |= NCOPTION_SUPPRESS_BANNERS | NCOPTION_DRAIN_INPUT;

    m_NC = notcurses_core_init(&opts, nullptr);
    if (!m_NC) {
        std::cerr << "Fatal Error: Failed to initialize notcurses for Windows Terminal." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    m_StdPlane = notcurses_stdplane(m_NC);

    ncplane_options hopts = {};
    hopts.y = 0;
    hopts.x = 0;
    hopts.rows = 1;
    hopts.cols = 80;
    hopts.flags = 0; 
    m_HudPlane = ncplane_create(m_StdPlane, &hopts);
}

NotcursesPresenter::~NotcursesPresenter() {
    if (!m_Headless && m_NC) {
        notcurses_stop(m_NC);
    }
}

void NotcursesPresenter::ResizeBuffers(int new_w, int new_h) {
    (void)new_w;
    (void)new_h;
}

bool NotcursesPresenter::TryUpdateTerminalSize(int fallback_w, int fallback_h) {
    if (m_Headless || !m_NC) {
        m_TermW = fallback_w;
        m_TermH = fallback_h;
        return true;
    }

    unsigned int rows, cols;
    notcurses_term_dim_yx(m_NC, &rows, &cols);
    m_TermW = cols;
    m_TermH = rows;
    return true;
}

void NotcursesPresenter::Present(const std::vector<Pixel>& framebuffer,
                                 const std::vector<float>& zbuffer, 
                                 int pix_w, int term_w, int term_h) {
    if (m_Headless || !m_NC || pix_w <= 0) return;

    uint32_t id;
    ncinput ni;
    while ((id = notcurses_get_nblock(m_NC, &ni)) != (uint32_t)-1) {
        if (id == 0) break; 
        if (id == 'q' || id == 'Q' || id == 27 /* ESC */) {
            notcurses_stop(m_NC);
            std::exit(EXIT_SUCCESS);
        }
    }

    int pix_h = static_cast<int>(framebuffer.size() / pix_w);

    // Braille dots standard layout offsets
    static const uint32_t dot_map[4][2] = {
        {0x01, 0x08},
        {0x02, 0x10},
        {0x04, 0x20},
        {0x40, 0x80}
    };

    // Calculate Braille directly into standard plane to avoid the massive visual overhead
    for (int ty = 0; ty < term_h; ++ty) {
        for (int tx = 0; tx < term_w; ++tx) {
            uint32_t braille_offset = 0;
            uint32_t r_acc = 0, g_acc = 0, b_acc = 0;
            int colored_pixels = 0;

            // Sample the 2x4 block for this terminal cell
            for (int dy = 0; dy < 4; ++dy) {
                int py = ty * 4 + dy;
                if (py >= pix_h) continue;

                for (int dx = 0; dx < 2; ++dx) {
                    int px = tx * 2 + dx;
                    if (px >= pix_w) continue;

                    int idx = py * pix_w + px;
                    if (zbuffer[idx] > 0.0f) {
                        braille_offset |= dot_map[dy][dx];
                        const Pixel& p = framebuffer[idx];
                        r_acc += p.r;
                        g_acc += p.g;
                        b_acc += p.b;
                        colored_pixels++;
                    }
                }
            }

            nccell cell = {};
            nccell_set_bg_rgb(&cell, 0x000000); // Erase trails

            if (colored_pixels > 0) {
                uint32_t fg_rgb = ((r_acc / colored_pixels) << 16) | 
                                  ((g_acc / colored_pixels) << 8)  | 
                                  (b_acc / colored_pixels);
                nccell_set_fg_rgb(&cell, fg_rgb);

                // UTF-8 encoding of (0x2800 + braille_offset)
                unsigned char b0 = 0xE2;
                unsigned char b1 = 0xA0 | ((braille_offset >> 6) & 0x03);
                unsigned char b2 = 0x80 | (braille_offset & 0x3F);

                // Manually map to the underlying gcluster UTF-8 LE capacity array
                // avoiding any reliance on C-library mbrtowc (which destroys performance
                // and fails on incomplete UTF-8 Windows Locales).
                cell.gcluster = b0 | (b1 << 8) | (b2 << 16);
                cell.width = 1;
            } else {
                cell.gcluster = 0x20; // ASCII Space for empty pixels
                cell.width = 1;
            }

            ncplane_putc_yx(m_StdPlane, ty, tx, &cell);
        }
    }

    notcurses_render(m_NC);
}

} // namespace tvk

#endif // TVK_NOTCURSES_AVAILABLE
