#include <algorithm>
#include <chrono>
#include <csignal>
#include <format>
#include "ExitStatus.h"
#include "Mandelbrot.h"

using timePoint = std::chrono::time_point<std::chrono::system_clock>;

size_t WIDTH = 1920;
size_t HEIGHT = 1080;
uint32_t MAX_ITER = 2000;
double FPS = 60.0;
double DURATION_SECONDS = 10.0;
std::string OUTPUT_FILE = "mandelbrot.mp4";
const double PROGRESS_UPDATE_INTERVAL = 0.5;
bool sigint_flag = false;

static double smooth(double a, double b, double t, double t0) {
    double x = t / t0;
    double smooth = x * x * (3 - 2 * x);
    return a + (b - a) * smooth;
}

#ifdef _WIN32
    #include <windows.h>
    static int get_terminal_width() {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            return csbi.srWindow.Right - csbi.srWindow.Left + 1;
        }
        return 100;
    }
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
    static int get_terminal_width() {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            return w.ws_col;
        }
        char *cols = getenv("COLUMNS");
        if (cols) return atoi(cols);
        return 100;
    }
#endif

static void progress(double progress) {
    int bar_width = std::max(0, get_terminal_width() - 70);
    if (bar_width <= 0) return;
    std::cout << "\r[";
    int pos = static_cast<int>(bar_width * progress);
    for (int i = 0; i < pos; ++i) std::cout << "=";
    if (pos < bar_width) std::cout << ">";
    for (int i = pos + 1; i < bar_width; ++i) std::cout << " ";
    std::cout << std::format("] {:>3} % ", static_cast<int>(progress * 100.0));
}

const std::unordered_map<std::string, int> subcommand_index = {
    {"-o", 0},
    {"--output", 0},
    {"-s", 1},
    {"--size", 1},
    {"--width", 2},
    {"--height", 3},
    {"--maxiter", 4},
    {"--fps", 5},
    {"--duration", 6}
};

static void analyze_command_line_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto it = subcommand_index.find(arg);
        if (it != subcommand_index.end()) {
            int index = it->second;
            if (i + 1 >= argc) {
                std::cerr << "Missing value for argument: " << arg << "\n";
                std::exit(InvalidArgument);
            }
            std::string value = argv[++i];
            switch (index) {
                case 0:
                    OUTPUT_FILE = value;
                    break;
                case 1:
                    {
                        size_t pos = value.find('x');
                        if (pos == std::string::npos) {
                            std::cerr << "Invalid size format. Please use WxH.\n";
                            std::exit(InvalidArgument);
                        }
                        WIDTH = std::stoi(value.substr(0, pos));
                        HEIGHT = std::stoi(value.substr(pos + 1));
                    }
                    break;
                case 2:
                    WIDTH = std::stoi(value);
                    break;
                case 3:
                    HEIGHT = std::stoi(value);
                    break;
                case 4:
                    MAX_ITER = std::stoi(value);
                    break;
                case 5:
                    FPS = std::stod(value);
                    break;
                case 6:
                    DURATION_SECONDS = std::stod(value);
                    break;
                default:
                    std::cerr << "Unsolved argument: " << arg << "\nPlease report this bug.\n";
                    std::exit(Bug);
            }
        }
        else if (arg == std::string("-h") || arg == std::string("--help")) {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -h, --help            Show this help message and exit\n"
                      << "  -o, --output <file>   Set output video file name (default: mandelbrot.mp4)\n"
                      << "  -s, --size <WxH>      Set video size (default: 1920x1080)\n"
                      << "  --width <value>   Set video width (default: 1920)\n"
                      << "  --height <value>  Set video height (default: 1080)\n"
                      << "  --maxiter <value>    Set maximum iterations (default: 1000)\n"
                      << "  --fps <value>        Set frames per second (default: 60)\n"
                      << "  --duration <value>   Set duration in seconds (default: 10)\n";
            std::exit(Success);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            std::exit(InvalidArgument);
        }
    }
}

static void sigint_handler(int signal) {
    std::cout << "\nInterrupt signal (" << signal << ") received. Exiting...\n";
    sigint_flag = true;
}

int main(int argc, char* argv[]) {
    analyze_command_line_args(argc, argv);
    std::signal(SIGINT, sigint_handler);
    Mandelbrot mandelbrot(WIDTH, HEIGHT, MAX_ITER);
    auto fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::VideoWriter writer(OUTPUT_FILE, fourcc, FPS, cv::Size(WIDTH, HEIGHT));
    if (!writer.isOpened()) {
        std::cerr << "Could not open the output video file for write.\n";
        std::exit(VideoWriterError);
    }
    std::cout << std::format("Video will be saved to {}. Using {} threads.\n", OUTPUT_FILE, std::max(1U, std::thread::hardware_concurrency()));
    double zoom;
    double render_speed = 0.0;
    double progress_update_elapsed = 0;
    int frame = 0;
    auto render_start_time = std::chrono::system_clock::now();
    for (; frame < FPS * DURATION_SECONDS; ++frame) {
        if (sigint_flag) break;
        zoom = 0.8 * std::pow(1.05, smooth(0.0, 280, frame, DURATION_SECONDS * FPS));
        mandelbrot.setView(zoom, -0.743643887037158704752191506114774, 0.131825904205311970493132056385139);
        timePoint start = std::chrono::system_clock::now();
        cv::Mat& image = mandelbrot.generate();
        timePoint end = std::chrono::system_clock::now();
        double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1'000'000.0;
        progress_update_elapsed += elapsed;
        render_speed = 0.63 * render_speed + 0.37 / elapsed;
        if (progress_update_elapsed > PROGRESS_UPDATE_INTERVAL) {
            progress(static_cast<double>(frame) / (FPS * DURATION_SECONDS));
            std::cout << std::format("Frame {:>3} rendered in {:#.4g} ms. Approximate speed {:#.4g} FPS.", frame, elapsed * 1000, render_speed) << std::flush;
            progress_update_elapsed = 0;
        }
        writer.write(image);
    }
    std::cout << "\r\033[2K";
    progress(1.0);
    writer.release();
    auto render_end_time = std::chrono::system_clock::now();
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(render_end_time - render_start_time).count();
    std::cout << std::format("\nTotal rendering time: {} ms. Average speed: {:.4g} FPS\n", totalElapsed, (frame) / (totalElapsed / 1000.0));
    return 0;
}