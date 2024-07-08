#include "rclcpp/rclcpp.hpp"

#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <string>
#include <iostream>

class GoProInterface : public rclcpp::Node {
public:
    GoProInterface(const std::string& ip_address)
    : Node("gopro_interface"), ip(ip_address), base_url("http://" + ip) {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~GoProInterface() {
        curl_global_cleanup();
    }

    bool send_curl_request(const std::string& url) {
        CURL* curl = curl_easy_init();
        if(curl) {
            CURLcode res;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Use HEAD for checking URL validity
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            return res == CURLE_OK;
        }
        return false;
    }

    bool enable_usb_control() {
        bool result = send_curl_request(base_url + "/gopro/camera/control/wired_usb?p=1");
        if (result) {
            RCLCPP_INFO(this->get_logger(), "Initialized USB successfully!");
            return true;
        }
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize USB");
        return false;
    }

    bool disable_usb_control() {
        bool result = send_curl_request(base_url + "/gopro/camera/control/wired_usb?p=0");
        if (result) {
            RCLCPP_INFO(this->get_logger(), "Disabled USB successfully!");
            return true;
        }
        RCLCPP_ERROR(this->get_logger(), "Failed to disable USB");
        return false;
    }

    bool start_recording(int64_t& timestamp) {
        std::string url = base_url + "/gopro/camera/shutter/start";
        bool result = send_curl_request(url);
        if (result) {
            timestamp = rclcpp::Clock().now().nanoseconds();
            RCLCPP_INFO(this->get_logger(), "Started Recording at timestamp - %ld", timestamp);
            return true;
        }
        RCLCPP_ERROR(this->get_logger(), "Failed to start recording at timestamp - %ld", timestamp);
        return false;
    }

    bool stop_recording(std::string &path, int64_t &timestamp) {
        std::string url = base_url + "/gopro/camera/shutter/stop";
        bool result = send_curl_request(url);
        if (result) {
            timestamp = rclcpp::Clock().now().nanoseconds();
            RCLCPP_INFO(this->get_logger(), "Stopped Recording at timestamp - %ld", timestamp);
            get_last_media_path(path);
            rclcpp::sleep_for(std::chrono::seconds(2)); // Wait for the file to be written
            return true;
        }
        RCLCPP_ERROR(this->get_logger(), "Failed to stop recording at timestamp - %ld", timestamp);
        path = "";
        timestamp = 0;
        return false;
    }

    // __unused__
    bool get_last_media_path2(std::string &path) {
        std::string url = base_url + "/gopro/media/list";

        CURL *hnd = curl_easy_init();

        curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(hnd, CURLOPT_URL, url.c_str());

        CURLcode ret = curl_easy_perform(hnd);

        //print the response
        // if (ret == CURLE_OK) {
        //     long response_code;
        //     curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &response_code);
        //     if (response_code == 200) {
        //         // get response dictionary and print
        //         std::string response;
        //         curl_easy_getinfo(hnd, CURLINFO_CONTENT_TYPE, &response);
        //         RCLCPP_INFO(this->get_logger(), "Response: %s", response.c_str());
        //     }
        // }

    }

    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s) {
        size_t newLength = size * nmemb;
        s->append(static_cast<char*>(contents), newLength);
        return newLength;
    }

    bool get_last_media_path(std::string &path) {
        std::string url = base_url + "/gopro/media/list";
        std::string response_data;

        CURL *hnd = curl_easy_init();
        if (hnd) {
            curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "GET");
            curl_easy_setopt(hnd, CURLOPT_URL, url.c_str());
            curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(hnd, CURLOPT_WRITEDATA, &response_data);

            CURLcode ret = curl_easy_perform(hnd);
            curl_easy_cleanup(hnd);

            if (ret != CURLE_OK) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(ret) << '\n';
                return false;
            }

            try {
                nlohmann::json j = nlohmann::json::parse(response_data);
                if (!j["media"].empty()) {
                    nlohmann::json last_media_object = j["media"].back(); // Access the last item in the "media" array
                    if (!last_media_object["fs"].empty()) {
                        nlohmann::json last_file_object = last_media_object["fs"].back(); // Access the last file in the "fs" array

                        std::string file_name = last_file_object["n"].get<std::string>();
                        if (file_name.find(".MP4") != std::string::npos) {
                            std::string directory = last_media_object["d"].get<std::string>();
                            path = directory + "/" + file_name;
                            return true;
                        }
                    }
                }
            } catch (nlohmann::json::parse_error& e) {
                std::cerr << "JSON parse error: " << e.what() << '\n';
                return false;
            }
        }
        return false;
    }

    

private:
    std::string ip;
    std::string base_url;

    static size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp) {
        ((std::string*)userp)->append((char*)buffer, size * nmemb);
        return size * nmemb;
    }
};

int main(int argc, char ** argv) {
    rclcpp::init(argc, argv);
    std::shared_ptr<GoProInterface> node = std::make_shared<GoProInterface>("172.20.134.51:8080");


    node->enable_usb_control();

    int64_t start_timestamp;
    node->start_recording(start_timestamp);

    // Simulate some operation time
    rclcpp::sleep_for(std::chrono::seconds(5));

    std::string path;
    int64_t stop_timestamp;
    node->stop_recording(path, stop_timestamp);

    node->disable_usb_control();

    if(!path.empty()){
        RCLCPP_INFO(node->get_logger(), "Last Path: %s", path.c_str());
    }

    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}

