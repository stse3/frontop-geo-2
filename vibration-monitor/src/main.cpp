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
#include <utility>   // for std::exchange
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>


#ifndef TIOCM_DTR
#define TIOCM_DTR       0x002  /* Data Terminal Ready */
#endif

#ifndef TIOCM_RTS
#define TIOCM_RTS       0x004  /* Request To Send */
#endif

#ifndef TIOCMGET
#define TIOCMGET        0x5415  /* Get status of modem bits */
#endif

#ifndef TIOCMSET
#define TIOCMSET        0x5418  /* Set status of modem bits */
#endif

#ifndef TIOCMBIS
#define TIOCMBIS        0x5416
#endif
#ifndef TIOCMBIC
#define TIOCMBIC        0x5417
#endif

using json = nlohmann::json;

struct VibrationData {
    std::string datetime;
    std::string type;       // HIST or TRIG
    std::string unit;       // mm/s or in/s
    float vppv{0}, lppv{0}, tppv{0};
    float vf{0}, lf{0}, tf{0};
    float pspl_dB{0}; //db value
    float reference_value{0}; // For TRIG events, 0 for HIST
    bool has_reference{false}; // Flag to indicate if reference value exists
    std::string sensor_sn; //sensor_sn loged

    VibrationData() = default;
    explicit VibrationData(const std::string& dt) : datetime(dt) {}
};

class Config {
    std::map<std::string, std::string> settings;
public:
    Config(const std::string& filename = "/etc/vibration.conf") {
        std::ifstream file(filename);
        if (!file) {
            setDefaults();
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                settings[key] = value;
            }
        }
        setDefaults();
    }
    
    std::string get(const std::string& key) const {
        auto it = settings.find(key);
        return it != settings.end() ? it->second : "";
    }
    
private:
    void setDefaults() {
        if (settings.find("tty_device") == settings.end()) settings["tty_device"] = "/dev/ttyUSB4";
        if (settings.find("threshold") == settings.end()) settings["threshold"] = "1.0";
        if (settings.find("data_dir") == settings.end()) settings["data_dir"] = "/tmp/vibration_data";
        if (settings.find("clear_events") == settings.end()) settings["clear_events"] = "false";
    }
};

class SerialPort {
    int fd;
    std::string port;
    termios tty;
    
    std::thread connection_thread;
    std::mutex connection_mutex;
    std::condition_variable connection_cv;
    std::atomic<bool> should_reconnect{true};
    std::atomic<bool> is_connected{false};

public:
    // Default constructor
    SerialPort() : fd(-1) {}

    // Constructor with port
    explicit SerialPort(const std::string& p) : fd(-1), port(p) {}

    // Delete copy constructor and copy assignment
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Move constructor
    SerialPort(SerialPort&& other) noexcept
        : fd(std::exchange(other.fd, -1)),
          port(std::move(other.port)),
          connection_thread(std::move(other.connection_thread)),
          should_reconnect(other.should_reconnect.load()),
          is_connected(other.is_connected.load()) {
        // Stop the other object's persistent connection
        other.stopPersistentConnection();
    }

    // Move assignment operator
    SerialPort& operator=(SerialPort&& other) noexcept {
        if (this != &other) {
            // Stop current connection
            stopPersistentConnection();

            // Clean up current resources
            if (fd >= 0) close(fd);

            // Move resources
            fd = std::exchange(other.fd, -1);
            port = std::move(other.port);
            connection_thread = std::move(other.connection_thread);
            should_reconnect = other.should_reconnect.load();
            is_connected = other.is_connected.load();

            // Stop the other object's persistent connection
            other.stopPersistentConnection();
        }
        return *this;
    }

    ~SerialPort() { 
        stopPersistentConnection();
        
        if(fd >= 0) close(fd); 
    }

    std::string getPort() const { return port; }

    
    void startPersistentConnection() {
        stopPersistentConnection();
        
        should_reconnect = true;
        connection_thread = std::thread([this]() {
            while (should_reconnect) {
                if (!is_connected) {
                    bool connection_result = open();
                    is_connected = connection_result;
                    
                    if (!connection_result) {
                        std::unique_lock<std::mutex> lock(connection_mutex);
                        connection_cv.wait_for(lock, 
                            std::chrono::seconds(10), 
                            [this]() { return !should_reconnect; });
                        continue;
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(5));
                
                if (!checkConnection()) {
                    is_connected = false;
                    if (fd >= 0) {
                        close(fd);
                        fd = -1;
                    }
                }
            }
        });
    }

    void stopPersistentConnection() {
        should_reconnect = false;
        is_connected = false;
        connection_cv.notify_all();
        
        if (connection_thread.joinable()) {
            connection_thread.join();
        }
    }

    bool isConnected() const {
        return is_connected;
    }

    bool checkConnection() {
        if (fd < 0) return false;
        
        try {
            int status;
            return (ioctl(fd, TIOCMGET, &status) >= 0);
        } catch (...) {
            return false;
        }
    }

    bool open() {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }

        fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd < 0) {
            std::cerr << "Failed to open " << port << ": " << strerror(errno) << std::endl;
            return false;
        }
        
        fcntl(fd, F_SETFL, 0);

        memset(&tty, 0, sizeof(tty));
        if (tcgetattr(fd, &tty) != 0) {
            std::cerr << "tcgetattr failed: " << strerror(errno) << std::endl;
            return false;
        }
        cfsetospeed(&tty, B9600);
        cfsetispeed(&tty, B9600);

        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |= CREAD | CLOCAL | HUPCL;

        tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON | IXOFF);
        
        tty.c_oflag &= ~OPOST;

        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 10;

        if (tcsetattr(fd, TCSANOW, &tty) != 0) {
            std::cerr << "tcsetattr failed: " << strerror(errno) << std::endl;
            return false;
        }

        usleep(100000);

        tcflush(fd, TCIOFLUSH);

        int flags;
        flags = TIOCM_DTR | TIOCM_RTS;
        ioctl(fd, TIOCMBIC, &flags);
        usleep(100000);
        ioctl(fd, TIOCMBIS, &flags);
        usleep(500000);

        tcflush(fd, TCIFLUSH);
        tcflush(fd, TCOFLUSH);
        tcflush(fd, TCIOFLUSH);

        is_connected = true;
        return true;
    }

    bool write(const std::string& data) {
        if (fd < 0) return false;
        std::cout << "TX: " << data << std::endl;
        ssize_t written = ::write(fd, data.c_str(), data.length());
        if (written < 0) return false;
        tcdrain(fd);
        return written == static_cast<ssize_t>(data.length());
    }

    std::string read(int timeout_ms = 1000) {
        if (fd < 0) return "";
        std::string result;
        char buffer[256];
        int total_ms = 0;
        const int chunk_ms = 100;
        int empty_reads = 0;
        const int max_empty_reads = 5;
        
        while (total_ms < timeout_ms) {
            ssize_t n = ::read(fd, buffer, sizeof(buffer)-1);
            if (n > 0) {
                buffer[n] = 0;
                result += buffer;
                empty_reads = 0;
            } else {
                empty_reads++;
                if (!result.empty() && empty_reads >= max_empty_reads) {
                    break;
                }
            }
            usleep(chunk_ms * 1000);
            total_ms += chunk_ms;
        }
        if (!result.empty()) std::cout << "RX: " << result << std::endl;
        return result;
    }
};

class SerialPortDetector {
private:
    static std::string findDeviceByVendorId(const std::string& vendor_id) {
        const std::string sys_path = "/sys/bus/usb/devices/";
        DIR* dir = opendir(sys_path.c_str());
        if (!dir) {
            std::cerr << "Failed to open " << sys_path << ": " << strerror(errno) << std::endl;
            return "";
        }

        std::string result;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;

            std::string id_path = sys_path + entry->d_name + "/idVendor";
            std::ifstream vendor_file(id_path);
            if (!vendor_file) continue;

            std::string found_vendor;
            vendor_file >> found_vendor;
            if (found_vendor == vendor_id) {
                // Search for ttyUSB device in this path
                std::string device_path = sys_path + entry->d_name;
                DIR* sub_dir = opendir(device_path.c_str());
                if (sub_dir) {
                    struct dirent* sub_entry;
                    while ((sub_entry = readdir(sub_dir)) != nullptr) {
                        std::string name = sub_entry->d_name;
                        if (name.find("ttyUSB") != std::string::npos) {
                            result = "/dev/" + name;
                            break;
                        }
                    }
                    closedir(sub_dir);
                }
                if (!result.empty()) break;
            }
        }
        closedir(dir);
        return result;
    }

public:
    static std::string detectSerialPort() {
        // First find all ttyUSB devices
        std::vector<std::string> tty_devices;
        DIR* dev_dir = opendir("/dev");
        if (!dev_dir) {
            std::cerr << "Failed to open /dev directory" << std::endl;
            return "";
        }

        struct dirent* entry;
        while ((entry = readdir(dev_dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.compare(0, 6, "ttyUSB") == 0) {
                tty_devices.push_back(name);
            }
        }
        closedir(dev_dir);

        // Now check each ttyUSB device's driver link
        for (const std::string& tty : tty_devices) {
            std::string path = "/sys/class/tty/" + tty + "/device/driver";
            char real_path[256];
            ssize_t len = readlink(path.c_str(), real_path, sizeof(real_path)-1);
            if (len != -1) {
                real_path[len] = '\0';
                std::string driver(real_path);
                std::string full_path = "/dev/" + tty;
                
                // Check if it's an FTDI or Prolific device
                bool is_supported = false;
                if (driver.find("ftdi_sio") != std::string::npos) {
                    std::cout << "Found FTDI device at " << full_path << std::endl;
                    is_supported = true;
                } else if (driver.find("pl2303") != std::string::npos) {
                    std::cout << "Found Prolific device at " << full_path << std::endl;
                    is_supported = true;
                }

                if (is_supported) {
                    // Verify we can open it
                    int fd = open(full_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
                    if (fd >= 0) {
                        close(fd);
                        return full_path;
                    } else {
                        std::cerr << "Found device at " << full_path << " but couldn't open it: " 
                                << strerror(errno) << std::endl;
                    }
                }
            }
        }
        
        std::cerr << "No supported serial device (FTDI or Prolific) found" << std::endl;
        return "";
    }
};

class SensorKeyRetriever {
    private:
        std::string apiUrl;
        std::string apiKey;
    
    public:
        SensorKeyRetriever(const Config& cfg)
            : apiUrl(cfg.get("get_sensor_cloud_url")), apiKey(cfg.get("get_sensor_api_key")) {}

        std::string getSensorKey(const std::string& sensorId) {
            CURL* curl = curl_easy_init();
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
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
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
                CURLcode res = curl_easy_perform(curl);
    
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
    
                if (res == CURLE_OK) {
                    return extractSensorKey(response);
                } else {
                    std::cerr << "cURL request failed: " << curl_easy_strerror(res) << std::endl;
                    return "";
                }
            }
            return "";
        }
    
    private:
        static size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
            data->append(ptr, size * nmemb);
            return size * nmemb;
        }
    
        static std::string extractSensorKey(const std::string& jsonResponse) {
            try {
                auto json = nlohmann::json::parse(jsonResponse);
                if (json.contains("sensor_key")) {
                    return json["sensor_key"].get<std::string>();
                }
            } catch (const std::exception& e) {
                std::cerr << "JSON parsing error: " << e.what() << std::endl;
            }
            return "";
        }
    };
    

class VibrationReader {
    SerialPort port;
    std::string logfile;
    std::string data_dir;
    bool connected;
    bool clear_events;
    Config cfg;
    std::string program_start_time;
    std::string sensor_sn;


    std::vector<VibrationData> parseEvents(const std::string& data) {
        std::vector<VibrationData> events;
        std::regex event_regex(R"(\. #\d+\s+\d+\s+(HIST|TRIG)\s+(\d{2}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2})\s+VLT\s+(\S+)\s+(\S+)\s+(\S+)\s+(mm/s|in/s)\s+(\S+)\s+(\S+)\s+(\S+)\s+HZ\s+(\S+)\s+DB(?:\s+R\s+(\S+)\s+in/s)?)");
        auto begin = std::sregex_iterator(data.begin(), data.end(), event_regex);
        auto end = std::sregex_iterator();
    
        for (auto i = begin; i != end; ++i) {
            // Get current time for each event to ensure timestamp accuracy
            std::smatch match = *i;
            VibrationData event;
            
            // Use program start time instead of current time for each event
            event.datetime = program_start_time;
    
            event.type = match[1];          // HIST or TRIG
            event.vppv = std::stof(match[3]);
            event.lppv = std::stof(match[4]);
            event.tppv = std::stof(match[5]);
            event.unit = match[6];          // mm/s or in/s
            event.vf = std::stof(match[7]);
            event.lf = std::stof(match[8]);
            event.tf = std::stof(match[9]);
            event.pspl_dB = std::stof(match[10]);
            
            // Check if reference value exists
            if (match[11].matched) {
                event.has_reference = true;
                event.reference_value = std::stof(match[11]);
            }
            event.sensor_sn = sensor_sn;
            std::cout << "Added event with timestamp: " << event.datetime << " and sensor SN: " << event.sensor_sn << std::endl;
            events.push_back(event);
            
            // Add debug logging
            std::cout << "Added event with timestamp: " << event.datetime << std::endl;
        }
        return events;
    }
    bool saveEvents(const std::vector<VibrationData>& events, const std::string& filename) {
        if (events.empty()) return true;

        std::string output_filename = filename;
        const auto first_sensor = events.front().sensor_sn;
        if (!first_sensor.empty() && filename != "/tmp/vibration_events.log") {
            const std::size_t dot_pos = filename.find_last_of('.');
            const std::size_t slash_pos = filename.find_last_of('/');
            const bool has_extension = dot_pos != std::string::npos && (slash_pos == std::string::npos || dot_pos > slash_pos);
            if (has_extension) {
                output_filename = filename.substr(0, dot_pos) + "_" + first_sensor + filename.substr(dot_pos);
            } else {
                output_filename = filename + "_" + first_sensor;
            }
        }

        // Create directories if they don't exist
        size_t last_slash = output_filename.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string dir = output_filename.substr(0, last_slash);
            std::string cmd = "mkdir -p " + dir;
            system(cmd.c_str());
        }

        // Open file in append mode
        std::ofstream file(output_filename.c_str(), std::ios::app);
        if (!file) {
            std::cerr << "Failed to open output file: " << output_filename << std::endl;
            return false;
        }

        // Write Events
        for (const auto& event : events) {
            std::cout << "Event timestamp: " << event.datetime << std::endl;
            
            file << event.datetime << " "
                 << event.vppv << " "
                 << event.lppv << " "
                 << event.tppv << " "
                 << event.vf << " "
                 << event.lf << " "
                 << event.tf << "\n";
        }

        std::cout << "Saved " << events.size() << " events to " << output_filename << std::endl;
        return true;
    }
    
public:
    
    void setStartTime(const std::string& time) {
        program_start_time = time;
    }
    VibrationReader(const Config& config) 
        : port(SerialPortDetector::detectSerialPort()),
          data_dir(config.get("data_dir")),
          connected(false),
          clear_events(config.get("clear_events") == "true"),
          cfg(config),
          program_start_time("") {
        if (port.getPort().empty()) {
            std::cerr << "No serial port detected, falling back to config value: " << config.get("tty_device") << std::endl;
            port = SerialPort(config.get("tty_device"));
        }
        if (system(("mkdir -p " + data_dir).c_str()) != 0) {
            std::cerr << "Failed to create directory: " << data_dir << std::endl;
        }
    }
    std::string extractSensorId(const std::string& response) {
        // Print the entire response for debugging
        std::cout << "Full response: [" << response << "]" << std::endl;
    
        // Try multiple extraction methods
        
        // Method 1: Regex with print debugging
        try {
            std::regex sensor_id_regex(R"(\.?\s*(\d+)\s+)");
            std::smatch match;
            
            if (std::regex_search(response, match, sensor_id_regex)) {
                std::cout << "Sensor Found: [" << match[1] << "]" << std::endl;
                return match[1];
            } else {
                std::cout << "No regex match found" << std::endl;
            }
        } catch (const std::regex_error& e) {
            std::cout << "Regex error: " << e.what() << std::endl;
            return "";
        }
        return "";
    }
    
    bool connect() {
        std::cout << "Opening port " << port.getPort() << std::endl;
        if (!port.open()) return false;
        
        std::cout << "Conditioning line..." << std::endl;
        // Minicom-style line conditioning
        for (int i = 0; i < 3; i++) {
            port.write("\r");
            usleep(100000);
            std::string response = port.read();
            if (response.find(">") != std::string::npos) {
                break;
            }
        }

        std::cout << "Sending initial CR..." << std::endl;
        port.write("\r");
        sleep(1);
        std::string response = port.read();
        if (response.find(">") == std::string::npos) {
            // Try alternate line endings if first attempt failed
            const char* endings[] = {"\n", "\r\n"};
            bool success = false;
            for (const char* ending : endings) {
                port.write(ending);
                usleep(100000);
                response = port.read();
                if (response.find(">") != std::string::npos) {
                    success = true;
                    break;
                }
            }
            if (!success) return false;
        }
        
        std::cout << "Sending inf command..." << std::endl;
        port.write("inf\r");
        sleep(1);
        response = port.read();
        std:: string sensor_id = extractSensorId(response); //changed 


        this->sensor_sn = sensor_id;
        std::cout <<"Detected sensor SN: " << this->sensor_sn <<std::endl;


        SensorKeyRetriever retriever(cfg); //changed
        std:: string sensorKey = retriever.getSensorKey(sensor_id);//changed 
        std:: cout <<sensorKey<<std::endl;
        // Get the key from inf and check the sensor name
        std::cout << "Sending key command..." << std::endl;
        port.write("key " + sensorKey + "\r");
        sleep(1); 
        response = port.read();
        if (response.find("OK") == std::string::npos) return false;
        connected = true;
        return true;
    }

    void collect(const std::string& filename) {
        if (!connected) {
            std::cout << "Not connected" << std::endl;
            return;
        }
        std::cout << "Halting measurement..." << std::endl;
        port.write("hlt\r");
        std::string response = port.read();
        if (response.find("OK") == std::string::npos)
        sleep(1);

        // code to read data from the serial port
        // 1. keep reading until we see end of data
        // 2. default 30 second timeout
        // 3. handle pauses between data being sent
        std::cout << "Reading events..." << std::endl;
        port.write("sum\r");
        sleep(1);  // Initial wait for command processing
        std::string data;
        const int max_total_wait = 30;  // Maximum 30 seconds total
        int elapsed = 0;
        bool complete = false;

        while (elapsed < max_total_wait) {
            std::string chunk = port.read(3000);  // 3 second timeout per read
            if (chunk.empty()) {
                if (!data.empty()) {
                    // If we have data but got an empty read, wait a bit longer to confirm it's done
                    sleep(1);
                    chunk = port.read(1000);
                    if (chunk.empty()) {
                        complete = true;
                        break;
                    }
                }
            }
            data += chunk;
            elapsed += 3;
            
            // Look for indicators that the data is complete:
            // 1. Found TMPL entry (usually the last entry)
            // 2. Got a prompt (">") after receiving data and nothing more for a while
            if ((data.find("TMPL") != std::string::npos) || 
                (data.find(">") != std::string::npos && chunk.empty())) {
                complete = true;
                break;
            }
        }

        if (!complete) {
            std::cerr << "Warning: Data collection may be incomplete after " << elapsed << " seconds" << std::endl;
        }
        std::cout << "Attempting to parse events from data: " << data << std::endl;
        auto events = parseEvents(data);
        std::cout << "Found " << events.size() << " events" << std::endl;

        // save events to a daily log file
        saveEvents(events, filename);

        // also save events to /tmp/tmp_events.log, which clear out and use for uploading
        saveEvents(events, "/tmp/vibration_events.log");

        if (clear_events) {
            std::cout << "Clearing events..." << std::endl;
            port.write("clr E\r");
            sleep(1);
        }

        std::cout << "Starting measurement..." << std::endl;
        port.write("run\r");
        sleep(1);
        response = port.read();
        if (response.find("OK") == std::string::npos) {
            std::cerr << "Failed to start measurement" << std::endl;
        }
    }
};

class BigQueryUploader {
    CURL* curl;
    std::string endpoint;
    std::string device_id;
    std::string api_key;

public:
    BigQueryUploader(const Config& cfg) :
        device_id(cfg.get("device_id")),
        endpoint(cfg.get("cloud_function_url")),
        api_key(cfg.get("api_key"))
    {
        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize CURL" << std::endl;
        }
    }

    bool uploadEvents(const std::vector<VibrationData>& events) {
        if (!curl) return false;

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // Optional: enable verbose debug output
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth_header = "X-API-Key: " + api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        bool all_success = true;
        for (const auto& event : events) {
            nlohmann::json j = {
                {"datetime", event.datetime},
                {"device_id", device_id},
                {"type", event.type},
                {"vppv", event.vppv},
                {"lppv", event.lppv},
                {"tppv", event.tppv},
                {"unit", event.unit},
                {"vf", event.vf},
                {"lf", event.lf},
                {"tf", event.tf},
                {"pspl_dB", event.pspl_dB},
                {"sensor_sn", event.sensor_sn}  
            };
            if (event.has_reference) {
                j["reference_value"] = event.reference_value;
            }
            
            std::string json_str = j.dump();
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_str.length());
            
            long http_code = 0;
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "Curl error: " << curl_easy_strerror(res) << std::endl;
                all_success = false;
                break;
            }
            
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code != 200) {
                std::cerr << "HTTP error: " << http_code << std::endl;
                all_success = false;
                break;
            }
        }
        
        curl_slist_free_all(headers);
        return all_success;
    }

    ~BigQueryUploader() {
        if (curl) curl_easy_cleanup(curl);
    }
};


std::string getTodayPath(const std::string& base_dir) {
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char date_buf[9];
    std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", tm);
    return base_dir + "/" + std::string(date_buf) + ".vib";
}

bool readEventsFromFile(const std::string& filename, std::vector<VibrationData>& events) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            std::istringstream iss(line);
            std::string field;
            VibrationData event;
            
            // Read fields in the new space-separated format:
            // DateTime Vppv Lppv Tppv Vf Lf Tf
            std::getline(iss, event.datetime, ' ');
            std::getline(iss, field, ' ');
            event.datetime += " " + field;
            std::getline(iss, field, ' '); event.vppv = std::stof(field);
            std::getline(iss, field, ' '); event.lppv = std::stof(field);
            std::getline(iss, field, ' '); event.tppv = std::stof(field);
            std::getline(iss, field, ' '); event.vf = std::stof(field);
            std::getline(iss, field, ' '); event.lf = std::stof(field);
            std::getline(iss, field, ' '); event.tf = std::stof(field);

            events.push_back(event);
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse values in line: " << line << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
            continue;  // Skip this line and try the next one
        }
    }

    std::cout << "Successfully read " << events.size() << " events from file" << std::endl;
    return !events.empty();
}
int main(int argc, char** argv) {
    // print the current date/time to stdout
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    
    // Capture the formatted timestamp in a string
    std::stringstream ss;
    ss << std::put_time(tm, "%c %Z");
    std::string start_timestamp = ss.str();
    
    // Print the timestamp
    std::cout << "Running program: " << start_timestamp << std::endl;

    // get config file path from command line argument, if provided
    std::string config_path = "/etc/vibration.conf";
    if (argc > 1) config_path = argv[1];
    
    Config config(config_path);
    VibrationReader reader(config);
    reader.setStartTime(start_timestamp);
    BigQueryUploader uploader(config);
    
    std::string today_file = getTodayPath(config.get("data_dir"));
    
    if (!reader.connect()) {
            std::cerr << "Failed to connect" << std::endl;
            return 1;
        }
        reader.collect(today_file);

    // From here on, both debug and normal mode follow the same path
    const std::string upload_file = "/tmp/vibration_events.log";
    std::cout << "Attempting to read upload file: " << upload_file << std::endl;
    std::vector<VibrationData> events;
    if (readEventsFromFile(upload_file, events)) {
        std::cout << "Found events to upload" << std::endl;
    } else {
        std::cerr << "Failed to read events file" << std::endl;
    }

    
    if (events.empty()) {
        std::cout << "No events to upload" << std::endl;
        return 0;
    }

    std::cout << "Uploading " << events.size() << " events..." << std::endl;
    std::cout << "Endpoint: " << config.get("cloud_function_url") << std::endl;
    std::cout << "MAC ID: " << config.get("device_id") << std::endl;
    std::cout << "API Key length: " << config.get("api_key").length() << std::endl;
    
    if (!uploader.uploadEvents(events)) {
        std::cerr << "Failed to upload events" << std::endl;
        return 1;
    }
    
    std::cout << "Upload complete" << std::endl;
    if (remove(upload_file.c_str()) == 0) {
        std::cout << "Cleared upload tracking file" << std::endl;
    } else {
        std::cerr << "Failed to clear upload tracking file: " << strerror(errno) << std::endl;
    }
    
    return 0;
}