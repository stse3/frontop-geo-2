#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <curl/curl.h>   // Add back curl
#include "json.hpp"
#include <thread>
#include <regex>
using json = nlohmann::json;

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

public:
    SerialPort() : fd(-1) {}
    explicit SerialPort(const std::string& p) : fd(-1), port(p) {}
    
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    SerialPort(SerialPort&& other) noexcept
        : fd(std::exchange(other.fd, -1)),
          port(std::move(other.port)) {
    }

    SerialPort& operator=(SerialPort&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) close(fd);
            fd = std::exchange(other.fd, -1);
            port = std::move(other.port);
        }
        return *this;
    }

    ~SerialPort() { 
        if(fd >= 0) close(fd); 
    }

    std::string getPort() const { return port; }

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


class SensorKeyRetriever {
private:
    std::string apiUrl;
    std::string apiKey;

public:
    SensorKeyRetriever(const Config& cfg)
        : apiUrl(cfg.get("get_sensor_cloud_url")), 
          apiKey(cfg.get("get_sensor_api_key")) {}

    std::string getSensorKey(const std::string& sensorId) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize curl" << std::endl;
            return "";
        }

        std::string response;
        try {
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

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
                return "";
            }

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);

            return extractSensorKey(response);
        } catch (const std::exception& e) {
            std::cerr << "Exception in getSensorKey: " << e.what() << std::endl;
            if (curl) curl_easy_cleanup(curl);
            return "";
        }
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

class SerialPortDetector {
public:
    static std::string detectSerialPort() {
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

        for (const std::string& tty : tty_devices) {
            std::string path = "/sys/class/tty/" + tty + "/device/driver";
            char real_path[256];
            ssize_t len = readlink(path.c_str(), real_path, sizeof(real_path)-1);
            if (len != -1) {
                real_path[len] = '\0';
                std::string driver(real_path);
                std::string full_path = "/dev/" + tty;
                
                bool is_supported = false;
                if (driver.find("ftdi_sio") != std::string::npos) {
                    std::cout << "Found FTDI device at " << full_path << std::endl;
                    is_supported = true;
                } else if (driver.find("pl2303") != std::string::npos) {
                    std::cout << "Found Prolific device at " << full_path << std::endl;
                    is_supported = true;
                }

                if (is_supported) {
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

class SensorSetup {
    private:
        Config config;
        SerialPort serialPort;
        SensorKeyRetriever keyRetriever;
        std::string sensorKey;
    
    public:
        SensorSetup() 
            : config("/etc/vibration.conf"), 
              keyRetriever(config) {
            std::string port = SerialPortDetector::detectSerialPort();
            if (port.empty()) {
                    std::cerr << "Failed to detect serial port. Exiting." << std::endl;
                    exit(1);
                }
            
            std::cout << "Using port: " << port << std::endl;
            serialPort = SerialPort(port);
        }
        
        bool connect() {
            std::cout << "Opening port " << serialPort.getPort() << std::endl;
            if (!serialPort.open()) return false;
        
            std::cout << "Conditioning line..." << std::endl;
            for (int i = 0; i < 3; i++) {
                serialPort.write("\r");
                usleep(100000);
                std::string response = serialPort.read();
                if (response.find(">") != std::string::npos) {
                    break;
                }
            }
        
            // Wait for the port to be ready
            std::cout << "Waiting for sensor to be ready..." << std::endl;
            std::string readinessResponse;
            while (true) {
                serialPort.write("\r");
                usleep(500000); // Sleep for half a second between checks
                readinessResponse = serialPort.read();
                if (readinessResponse.find(">") != std::string::npos) {
                    break;
                }
            }
        
            // Get sensor ID and retrieve key
            std::cout << "Getting sensor ID..." << std::endl;
            serialPort.write("inf\r");
            std::string response = serialPort.read(2000);
            std::string sensorId = extractSensorId(response);
            if (sensorId.empty()) {
                std::cerr << "Failed to get sensor ID" << std::endl;
                return false;
            }
        
            std::cout << "Retrieving sensor key..." << std::endl;
            sensorKey = keyRetriever.getSensorKey(sensorId);
            if (sensorKey.empty()) {
                std::cerr << "Failed to get sensor key" << std::endl;
                return false;
            }
        
            std::cout << "Sending key command..." << std::endl;
            serialPort.write("key " + sensorKey + "\r");
            sleep(1);
            response = serialPort.read();
            if (response.find("OK") == std::string::npos) {
                std::cerr << "Key authentication failed." << std::endl;
                return false;
            }
        
            return true;
        }
        
        bool run() {
            std::cout << "Starting sensor setup..." << std::endl;

            if (!connect()) {
                std::cerr << "Failed to connect and initialize sensor." << std::endl;
                return false;
            }

        // Send 'hlt' command after setting parameters
        std::cout << "Sending hlt command..." << std::endl;
        serialPort.write("hlt\r");
        std::string response = serialPort.read(2000);
        std::cout << "hlt command response: [" << response << "]" << std::endl;

        // Send and verify the set commands
        std::cout << "Configuring sensor parameters..." << std::endl;

        // Send 'set 11 2' command
        if (!sendCommandAndVerify("set 11 2", "get 11")) {
            std::cerr << "Failed to set parameter 11." << std::endl;
            return false;
        }

        // Send 'set 35 13' command
        if (!sendCommandAndVerify("set 35 13", "get 35")) {
            std::cerr << "Failed to set parameter 35." << std::endl;
            return false;
        }

        // Add a delay before the final run command
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Send 'run' command to start the sensor
        std::cout << "Sending run command..." << std::endl;
        serialPort.write("run\r");
        response = serialPort.read(2000);
        std::cout << "run command response: [" << response << "]" << std::endl;

        std::cout << "Sensor configuration completed successfully!" << std::endl;
        // Clear events after setup
        std::cout << "Clearing events log..." << std::endl;
        serialPort.write("clr E\r");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        response = serialPort.read(2000);
        std::cout << "Clear events response: [" << response << "]" << std::endl;
        return true;

    }

private:
    std::string extractSensorId(const std::string& response) {
        std::cout << "Full response: [" << response << "]" << std::endl;
        
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

    bool sendCommandAndVerify(const std::string& setCommand, const std::string& getCommand) {
        std::cout << "Sending command: " << setCommand << std::endl;
        serialPort.write(setCommand + "\r");
        
        std::string response = serialPort.read(3000);
        std::cout << "Set command response: [" << response << "]" << std::endl;
        
        bool commandAccepted = response.find("OK") != std::string::npos || 
                               response.find(">") != std::string::npos;
                               
        if (!commandAccepted) {
            std::cerr << "Set command failed: " << setCommand << std::endl;
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << "Verifying with: " << getCommand << std::endl;
        serialPort.write(getCommand + "\r");
        response = serialPort.read(2000);
        std::cout << "Get command response: [" << response << "]" << std::endl;
        
        if (response.empty() || response.find(">") == std::string::npos) {
            std::cerr << "Get command failed: " << getCommand << std::endl;
            return false;
        }
        
        return true;
    }
};
int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    
    try {
        SensorSetup setup;
        std::cout << "Running sensor-setup utility..." << std::endl;
        
        bool success = setup.run();
        if (!success) {
            std::cerr << "Error: Sensor setup failed." << std::endl;
            curl_global_cleanup();
            return 1;
        }
        
        std::cout << "Sensor setup completed successfully." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        curl_global_cleanup();
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred." << std::endl;
        curl_global_cleanup();
        return 1;
    }
    
    curl_global_cleanup();
    return 0;
}