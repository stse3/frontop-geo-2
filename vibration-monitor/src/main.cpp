#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <string>
#include <string.h>
#include <vector>
#include <ctime>
#include <sstream>
#include <fstream>
#include <map>
#include <regex>
#include <iomanip>
#include <curl/curl.h>
#include "json.hpp"
#include <sys/ioctl.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <utility>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#ifndef TIOCM_DTR
#define TIOCM_DTR       0x002
#endif
#ifndef TIOCM_RTS
#define TIOCM_RTS       0x004
#endif
#ifndef TIOCMGET
#define TIOCMGET        0x5415
#endif
#ifndef TIOCMSET
#define TIOCMSET        0x5418
#endif
#ifndef TIOCMBIS
#define TIOCMBIS        0x5416
#endif
#ifndef TIOCMBIC
#define TIOCMBIC        0x5417
#endif

using json = nlohmann::json;

// ─── FIX 1: buildHeaders moved to global scope (was illegally nested inside streamEvents) ───
static curl_slist* buildHeaders(const std::string& api_key,
                                const std::string& key_name = "VM_API_KEY") {
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!api_key.empty()) {
        std::string header = key_name + ": " + api_key;
        headers = curl_slist_append(headers, header.c_str());
    }
    return headers;
}

// ─────────────────────────────────────────────────────────────────────────────

struct VibrationData {
    std::string datetime;
    std::string type;
    std::string unit;
    float vppv{0}, lppv{0}, tppv{0};
    float vf{0}, lf{0}, tf{0};
    float pspl_dB{0};
    float reference_value{0};
    bool has_reference{false};
    std::string sensor_sn;
    int battery_percent{-1};

    VibrationData() = default;
    explicit VibrationData(const std::string& dt) : datetime(dt) {}
};

class Config {
    std::map<std::string, std::string> settings;
public:
    Config(const std::string& filename = "/etc/vibration.conf") {
        std::ifstream file(filename);
        if (!file) { setDefaults(); return; }
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t pos = line.find('=');
            if (pos != std::string::npos)
                settings[line.substr(0, pos)] = line.substr(pos + 1);
        }
        setDefaults();
    }

    std::string get(const std::string& key) const {
        auto it = settings.find(key);
        return it != settings.end() ? it->second : "";
    }

private:
    void setDefaults() {
        if (settings.find("tty_device")    == settings.end()) settings["tty_device"]    = "/dev/ttyUSB4";
        if (settings.find("threshold")     == settings.end()) settings["threshold"]     = "1.0";
        if (settings.find("data_dir")      == settings.end()) settings["data_dir"]      = "/tmp/vibration_data";
        if (settings.find("clear_events")  == settings.end()) settings["clear_events"]  = "false";
        if (settings.find("vm_endpoint")   == settings.end()) settings["vm_endpoint"]   = "";
        if (settings.find("vm_api_key")    == settings.end()) settings["vm_api_key"]    = "";
    }
};

class VmEventStreamer {
    CURL* curl;
    std::string endpoint;
    std::string api_key;
    std::string device_id;

public:
    explicit VmEventStreamer(const Config& cfg)
        : curl(nullptr),
          endpoint(cfg.get("vm_endpoint")),
          api_key(cfg.get("vm_api_key")),
          device_id(cfg.get("device_id")) {
        if (!endpoint.empty()) {
            curl = curl_easy_init();
            if (!curl)
                std::cerr << "Failed to initialize CURL for VM streaming" << std::endl;
        }
    }

    bool enabled() const { return curl != nullptr && !endpoint.empty(); }

    bool streamEvents(const std::vector<VibrationData>& events) {
        if (!enabled() || events.empty()) return true;

        // FIX 1 (continued): buildHeaders is now a proper global function; call it here
        curl_slist* headers = buildHeaders(api_key);

        curl_easy_setopt(curl, CURLOPT_URL,        endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POST,        1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,     5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL,    1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,  headers);

        bool all_success = true;

        for (const auto& event : events) {
            nlohmann::json payload = {
                {"datetime",        event.datetime},
                {"device_id",       device_id},
                {"type",            event.type},
                {"unit",            event.unit},
                {"vppv",            event.vppv},
                {"lppv",            event.lppv},
                {"tppv",            event.tppv},
                {"vf",              event.vf},
                {"lf",              event.lf},
                {"tf",              event.tf},
                {"pspl_dB",         event.pspl_dB},
                {"sensor_sn",       event.sensor_sn},
                {"battery_percent", event.battery_percent}
            };
            if (event.has_reference)
                payload["reference_value"] = event.reference_value;

            std::string body = payload.dump();
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.length());

            long http_code = 0;
            CURLcode res = curl_easy_perform(curl);

            if (res != CURLE_OK) {
                std::cerr << "VM stream curl error: " << curl_easy_strerror(res) << std::endl;
                all_success = false;
                continue;
            }
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code < 200 || http_code >= 300) {
                std::cerr << "VM stream HTTP error: " << http_code << std::endl;
                all_success = false;
            }
        }

        curl_slist_free_all(headers);   // FIX 2: always free, even on partial failure
        return all_success;
    }

    ~VmEventStreamer() {
        if (curl) curl_easy_cleanup(curl);
    }
};

class SerialPort {
    // FIX 3: fd is now protected by fd_mutex so reconnect thread can't race main thread
    mutable std::mutex fd_mutex;
    int fd;
    std::string port_name;
    termios tty;

    std::thread connection_thread;
    std::mutex connection_mutex;
    std::condition_variable connection_cv;
    std::atomic<bool> should_reconnect{true};
    std::atomic<bool> is_connected{false};

    // Internal helpers that assume fd_mutex is already held by caller
    void closeFdLocked() {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

public:
    SerialPort() : fd(-1) {}
    explicit SerialPort(const std::string& p) : fd(-1), port_name(p) {}

    SerialPort(const SerialPort&)            = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // FIX 4: move constructor stops reconnect thread before touching fd
    SerialPort(SerialPort&& other) noexcept : fd(-1) {
        other.stopPersistentConnection();
        std::lock_guard<std::mutex> lk(other.fd_mutex);
        fd        = std::exchange(other.fd, -1);
        port_name = std::move(other.port_name);
        should_reconnect.store(other.should_reconnect.load());
        is_connected.store(other.is_connected.load());
    }

    SerialPort& operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        stopPersistentConnection();
        other.stopPersistentConnection();

        closeFdLocked();

        fd = std::exchange(other.fd, -1);
        port_name = std::move(other.port_name);

        should_reconnect.store(other.should_reconnect.load());
        is_connected.store(other.is_connected.load());
    }

    return *this;
}

    ~SerialPort() {
        stopPersistentConnection();
        std::lock_guard<std::mutex> lk(fd_mutex);
        closeFdLocked();
    }

    std::string getPort() const { return port_name; }

    void startPersistentConnection() {
        stopPersistentConnection();
        should_reconnect = true;
        connection_thread = std::thread([this]() {
            while (should_reconnect) {
                if (!is_connected) {
                    bool ok = open();
                    is_connected = ok;
                    if (!ok) {
                        std::unique_lock<std::mutex> lk(connection_mutex);
                        connection_cv.wait_for(lk, std::chrono::seconds(10),
                            [this]{ return !should_reconnect.load(); });
                        continue;
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (!checkConnection()) {
                    is_connected = false;
                    std::lock_guard<std::mutex> lk(fd_mutex);
                    closeFdLocked();
                }
            }
        });
    }

    void stopPersistentConnection() {
        should_reconnect = false;
        is_connected     = false;
        connection_cv.notify_all();
        if (connection_thread.joinable())
            connection_thread.join();
    }

    bool isConnected() const { return is_connected; }

    bool checkConnection() {
        std::lock_guard<std::mutex> lk(fd_mutex);
        if (fd < 0) return false;
        int status = 0;
        return (ioctl(fd, TIOCMGET, &status) >= 0);
    }

    bool open() {
        std::lock_guard<std::mutex> lk(fd_mutex);
        closeFdLocked();

        fd = ::open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd < 0) {
            std::cerr << "Failed to open " << port_name << ": " << strerror(errno) << std::endl;
            return false;
        }
        fcntl(fd, F_SETFL, 0);

        memset(&tty, 0, sizeof(tty));
        if (tcgetattr(fd, &tty) != 0) {
            std::cerr << "tcgetattr failed: " << strerror(errno) << std::endl;
            closeFdLocked(); return false;
        }
        cfsetospeed(&tty, B9600);
        cfsetispeed(&tty, B9600);

        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |=  CS8;
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |=  CREAD | CLOCAL | HUPCL;
        tty.c_iflag &= ~(IGNBRK|BRKINT|ICRNL|INLCR|PARMRK|INPCK|ISTRIP|IXON|IXOFF);
        tty.c_oflag &= ~OPOST;
        tty.c_lflag &= ~(ICANON|ECHO|ECHOE|ISIG|IEXTEN);
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 10;

        if (tcsetattr(fd, TCSANOW, &tty) != 0) {
            std::cerr << "tcsetattr failed: " << strerror(errno) << std::endl;
            closeFdLocked(); return false;
        }

        usleep(100000);
        tcflush(fd, TCIOFLUSH);

        int flags = TIOCM_DTR | TIOCM_RTS;
        ioctl(fd, TIOCMBIC, &flags);
        usleep(100000);
        ioctl(fd, TIOCMBIS, &flags);
        usleep(500000);
        tcflush(fd, TCIOFLUSH);

        is_connected = true;
        return true;
    }

    bool write(const std::string& data) {
        std::lock_guard<std::mutex> lk(fd_mutex);
        if (fd < 0) return false;
        std::cout << "TX: " << data << std::endl;
        ssize_t written = ::write(fd, data.c_str(), data.length());
        if (written < 0) return false;
        tcdrain(fd);
        return written == static_cast<ssize_t>(data.length());
    }

    std::string read(int timeout_ms = 1000) {
        std::lock_guard<std::mutex> lk(fd_mutex);
        if (fd < 0) return "";
        std::string result;
        char buffer[256];
        int total_ms = 0;
        const int chunk_ms = 100;
        int empty_reads = 0;
        const int max_empty_reads = 5;

        while (total_ms < timeout_ms) {
            ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = 0;
                result += buffer;
                empty_reads = 0;
            } else {
                ++empty_reads;
                if (!result.empty() && empty_reads >= max_empty_reads) break;
            }
            usleep(chunk_ms * 1000);
            total_ms += chunk_ms;
        }
        if (!result.empty()) std::cout << "RX: " << result << std::endl;
        return result;
    }
};

class SerialPortDetector {
public:
    static std::string detectSerialPort() {
        std::vector<std::string> tty_devices;
        DIR* dev_dir = opendir("/dev");
        if (!dev_dir) { std::cerr << "Failed to open /dev" << std::endl; return ""; }

        struct dirent* entry;
        while ((entry = readdir(dev_dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.compare(0, 6, "ttyUSB") == 0)
                tty_devices.push_back(name);
        }
        closedir(dev_dir);

        for (const std::string& tty : tty_devices) {
            std::string path = "/sys/class/tty/" + tty + "/device/driver";
            char real_path[256];
            ssize_t len = readlink(path.c_str(), real_path, sizeof(real_path) - 1);
            if (len == -1) continue;
            real_path[len] = '\0';
            std::string driver(real_path);
            std::string full_path = "/dev/" + tty;

            bool supported = (driver.find("ftdi_sio") != std::string::npos ||
                              driver.find("pl2303")   != std::string::npos);
            if (!supported) continue;

            std::cout << "Found supported device at " << full_path << std::endl;
            int fd = ::open(full_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (fd >= 0) { ::close(fd); return full_path; }
            std::cerr << "Could not open " << full_path << ": " << strerror(errno) << std::endl;
        }
        std::cerr << "No supported serial device found" << std::endl;
        return "";
    }
};

class SensorKeyRetriever {
    std::string apiUrl;
    std::string apiKey;

public:
    SensorKeyRetriever(const Config& cfg)
        : apiUrl(cfg.get("get_sensor_cloud_url")),
          apiKey(cfg.get("get_sensor_api_key")) {}

    std::string getSensorKey(const std::string& sensorId) {
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        curl_easy_setopt(curl, CURLOPT_URL,           apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_POST,           1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("X-API-Key: " + apiKey).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string requestBody = "{\"sensor_sn\": \"" + sensorId + "\"}";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "cURL request failed: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
        return extractSensorKey(response);
    }

private:
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

    static std::string extractSensorKey(const std::string& jsonResponse) {
        try {
            auto j = nlohmann::json::parse(jsonResponse);
            if (j.contains("sensor_key"))
                return j["sensor_key"].get<std::string>();
        } catch (const std::exception& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
        }
        return "";
    }
};

class VibrationReader {
    SerialPort port;
    std::string data_dir;
    bool connected;
    bool clear_events;
    Config cfg;
    VmEventStreamer vm_streamer;
    std::string program_start_time;
    std::string sensor_sn;
    int battery_percent{-1};

    // FIX 5: safe stof wrapper — bad firmware strings won't crash the process
    static float safeStof(const std::string& s, float fallback = 0.0f) {
        try { return std::stof(s); } catch (...) { return fallback; }
    }

    std::vector<VibrationData> parseEvents(const std::string& data) {
        std::vector<VibrationData> events;
        std::regex event_regex(
            R"(\. #\d+\s+\d+\s+(HIST|TRIG)\s+(\d{2}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2})\s+)"
            R"(VLT\s+(\S+)\s+(\S+)\s+(\S+)\s+(mm/s|in/s)\s+(\S+)\s+(\S+)\s+(\S+)\s+HZ\s+(\S+)\s+DB)"
            R"((?:\s+R\s+(\S+)\s+in/s)?)");

        auto begin = std::sregex_iterator(data.begin(), data.end(), event_regex);
        auto end   = std::sregex_iterator();

        for (auto i = begin; i != end; ++i) {
            std::smatch match = *i;
            VibrationData event;

            // FIX 6: use the timestamp from the device response, not program_start_time
            // match[2] is the device-reported date/time: "MM/DD/YY HH:MM:SS"
            event.datetime = match[2].str();

            event.type = match[1];
            event.vppv = safeStof(match[3]);
            event.lppv = safeStof(match[4]);
            event.tppv = safeStof(match[5]);
            event.unit = match[6];
            event.vf   = safeStof(match[7]);
            event.lf   = safeStof(match[8]);
            event.tf   = safeStof(match[9]);
            event.pspl_dB = safeStof(match[10]);

            if (match[11].matched) {
                event.has_reference    = true;
                event.reference_value  = safeStof(match[11]);
            }
            event.sensor_sn      = sensor_sn;
            event.battery_percent = battery_percent;

            std::cout << "Added event  datetime=" << event.datetime
                      << "  sensor_sn=" << event.sensor_sn << std::endl;
            events.push_back(event);
        }
        return events;
    }

    bool saveEvents(const std::vector<VibrationData>& events, const std::string& filename) {
        if (events.empty()) return true;

        std::string output_filename = filename;
        const auto& first_sensor = events.front().sensor_sn;
        if (!first_sensor.empty() && filename != "/tmp/vibration_events.log") {
            size_t dot_pos   = filename.find_last_of('.');
            size_t slash_pos = filename.find_last_of('/');
            bool has_ext = dot_pos != std::string::npos &&
                           (slash_pos == std::string::npos || dot_pos > slash_pos);
            output_filename = has_ext
                ? filename.substr(0, dot_pos) + "_" + first_sensor + filename.substr(dot_pos)
                : filename + "_" + first_sensor;
        }

        size_t last_slash = output_filename.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string dir = output_filename.substr(0, last_slash);
            system(("mkdir -p " + dir).c_str());
        }

        std::ofstream file(output_filename, std::ios::app);
        if (!file) {
            std::cerr << "Failed to open output file: " << output_filename << std::endl;
            return false;
        }

        for (const auto& event : events) {
            file << event.datetime    << " "
                 << event.vppv        << " "
                 << event.lppv        << " "
                 << event.tppv        << " "
                 << event.vf          << " "
                 << event.lf          << " "
                 << event.tf          << " "
                 << event.battery_percent << "\n";
        }
        std::cout << "Saved " << events.size() << " events to " << output_filename << std::endl;
        return true;
    }

public:
    void setStartTime(const std::string& time) { program_start_time = time; }

    VibrationReader(const Config& config)
        : port(SerialPortDetector::detectSerialPort()),
          data_dir(config.get("data_dir")),
          connected(false),
          clear_events(config.get("clear_events") == "true"),
          cfg(config),
          vm_streamer(config),
          program_start_time("") {
        if (port.getPort().empty()) {
            std::cerr << "No serial port detected, falling back to config: "
                      << config.get("tty_device") << std::endl;
            port = SerialPort(config.get("tty_device"));
        }
        if (system(("mkdir -p " + data_dir).c_str()) != 0)
            std::cerr << "Failed to create directory: " << data_dir << std::endl;
    }

    std::string extractSensorId(const std::string& response) {
        std::cout << "Full response: [" << response << "]" << std::endl;
        try {
            std::regex sensor_id_regex(R"(\.?\s*(\d+)\s+)");
            std::smatch match;
            if (std::regex_search(response, match, sensor_id_regex)) {
                std::cout << "Sensor Found: [" << match[1] << "]" << std::endl;
                return match[1];
            }
            std::cout << "No sensor ID match found" << std::endl;
        } catch (const std::regex_error& e) {
            std::cerr << "Regex error: " << e.what() << std::endl;
        }
        return "";
    }

    int extractBatteryPercent(const std::string& response) {
        try {
            std::regex battery_regex(R"(\(\s*\d+\s*,\s*(\d+)%\s*\))");
            std::smatch match;
            if (std::regex_search(response, match, battery_regex)) {
                std::cout << "Battery Found: [" << match[1] << "%]" << std::endl;
                return std::stoi(match[1]);
            }
            std::cout << "No battery match found" << std::endl;
        } catch (const std::regex_error& e) {
            std::cerr << "Regex error: " << e.what() << std::endl;
        }
        return -1;
    }

    bool connect() {
        std::cout << "Opening port " << port.getPort() << std::endl;
        if (!port.open()) return false;

        std::cout << "Conditioning line..." << std::endl;
        for (int i = 0; i < 3; i++) {
            port.write("\r");
            usleep(100000);
            if (port.read().find(">") != std::string::npos) break;
        }

        std::cout << "Sending initial CR..." << std::endl;
        port.write("\r");
        sleep(1);
        std::string response = port.read();
        if (response.find(">") == std::string::npos) {
            bool success = false;
            for (const char* ending : {"\n", "\r\n"}) {
                port.write(ending);
                usleep(100000);
                response = port.read();
                if (response.find(">") != std::string::npos) { success = true; break; }
            }
            if (!success) return false;
        }

        std::cout << "Sending inf command..." << std::endl;
        port.write("inf\r");
        sleep(1);
        response        = port.read();
        sensor_sn       = extractSensorId(response);
        battery_percent = extractBatteryPercent(response);
        std::cout << "Detected sensor SN: " << sensor_sn << std::endl;
        if (battery_percent >= 0)
            std::cout << "Detected battery: " << battery_percent << "%" << std::endl;

        SensorKeyRetriever retriever(cfg);
        std::string sensorKey = retriever.getSensorKey(sensor_sn);

        // FIX 7: guard against empty key before sending command
        if (sensorKey.empty()) {
            std::cerr << "Failed to retrieve sensor key — aborting connect" << std::endl;
            return false;
        }

        std::cout << "Sending key command..." << std::endl;
        port.write("key " + sensorKey + "\r");
        sleep(1);
        response = port.read();
        if (response.find("OK") == std::string::npos) return false;

        connected = true;
        return true;
    }

    void collect(const std::string& filename) {
        if (!connected) { std::cout << "Not connected" << std::endl; return; }

        std::cout << "Halting measurement..." << std::endl;
        port.write("hlt\r");
        port.read();   // consume response (OK or otherwise)
        sleep(1);

        std::cout << "Reading events..." << std::endl;
        port.write("sum\r");
        sleep(1);

        std::string data;
        const int max_total_wait = 30;
        int elapsed = 0;
        bool complete = false;

        while (elapsed < max_total_wait) {
            std::string chunk = port.read(3000);
            if (chunk.empty()) {
                if (!data.empty()) {
                    sleep(1);
                    chunk = port.read(1000);
                    if (chunk.empty()) { complete = true; break; }
                }
            }
            data    += chunk;
            elapsed += 3;

            if (data.find("TMPL") != std::string::npos ||
                (data.find(">") != std::string::npos && chunk.empty())) {
                complete = true;
                break;
            }
        }

        if (!complete)
            std::cerr << "Warning: data collection may be incomplete after "
                      << elapsed << "s" << std::endl;

        std::cout << "Parsing events from data..." << std::endl;
        auto events = parseEvents(data);
        std::cout << "Found " << events.size() << " events" << std::endl;

        if (!vm_streamer.streamEvents(events))
            std::cerr << "One or more events failed to stream to VM endpoint" << std::endl;

        saveEvents(events, filename);
        saveEvents(events, "/tmp/vibration_events.log");

        if (clear_events) {
            std::cout << "Clearing events..." << std::endl;
            port.write("clr E\r");
            sleep(1);
        }

        std::cout << "Starting measurement..." << std::endl;
        port.write("run\r");
        sleep(1);
        if (port.read().find("OK") == std::string::npos)
            std::cerr << "Failed to start measurement" << std::endl;
    }
};

// ─── Utilities ───────────────────────────────────────────────────────────────

std::string getTodayPath(const std::string& base_dir) {
    std::time_t now = std::time(nullptr);
    std::tm* tm     = std::localtime(&now);
    char date_buf[9];
    std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", tm);
    return base_dir + "/" + std::string(date_buf) + ".vib";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::time_t now = std::time(nullptr);
    std::tm* tm     = std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(tm, "%c %Z");
    std::string start_timestamp = ss.str();
    std::cout << "Running program: " << start_timestamp << std::endl;

    std::string config_path = "/etc/vibration.conf";
    if (argc > 1) config_path = argv[1];

    Config config(config_path);
    VibrationReader reader(config);
    reader.setStartTime(start_timestamp);

    std::string today_file = getTodayPath(config.get("data_dir"));

    if (!reader.connect()) {
        std::cerr << "Failed to connect" << std::endl;
        return 1;
    }
    reader.collect(today_file);

    return 0;
}