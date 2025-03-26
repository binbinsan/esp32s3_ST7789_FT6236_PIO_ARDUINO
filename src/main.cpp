#include <lvgl.h>
#include <TFT_eSPI.h>
#include <FT6236.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUDP.h>
#include <NTPClient.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <PubSubClient.h>  // Add MQTT library
#include <map>

// 开发板配置
#define BOARD_ESP32S3  // 如果使用ESP32，请注释此行
//#define BOARD_ESP32C3  // 如果使用ESP32C3，请取消注释此行

// MQTT配置
char mqtt_server[40] = "";
char mqtt_port[6] = "1883";
char mqtt_user[32] = "";
char mqtt_password[32] = "";
char mqtt_topic_prefix[32] = "smartclock/";  // MQTT主题前缀
char mqtt_topic_gpio[32] = "gpio/";          // GPIO主题
char mqtt_topic_brightness[32] = "brightness/"; // 亮度主题
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
bool mqtt_connected = false;

// I2C配置
#ifdef BOARD_ESP32S3
    #define I2C_SDA 3          // I2C SDA引脚
    #define I2C_SCL 2          // I2C SCL引脚
    #define TOUCH_SENSITIVITY 40  // 触摸灵敏度
#elif defined(BOARD_ESP32C3)
    #define I2C_SDA 4          // I2C SDA引脚
    #define I2C_SCL 5          // I2C SCL引脚
    #define TOUCH_SENSITIVITY 40  // 触摸灵敏度
#else
    #define I2C_SDA 8          // I2C SDA引脚
    #define I2C_SCL 9          // I2C SCL引脚
    #define TOUCH_SENSITIVITY 40  // 触摸灵敏度
#endif

// 全局变量
bool use_24h_format = true;  // 默认使用24小时制
bool show_date = true;       // 默认显示日期
time_t last_sync_time = 0;   // 上次同步时间

// 页面相关变量
lv_obj_t* boot_page;        // 启动页面
lv_obj_t* main_page;        // 主页面
lv_obj_t* wifi_page;        // WiFi信息页面
lv_obj_t* gpio_page;        // GPIO状态页面
lv_obj_t* wifi_scan_page;   // WiFi扫描页面
lv_obj_t* mqtt_page;        // MQTT页面
lv_obj_t* mqtt_config_page; // MQTT配置页面
lv_obj_t* wifi_info_label;  // WiFi详细信息标签
lv_obj_t* wifi_list_label;  // WiFi列表标签
lv_obj_t* mqtt_info_label;  // MQTT状态信息标签
bool is_wifi_page_shown = false;  // WiFi页面显示状态
bool is_gpio_page_shown = false;  // GPIO页面显示状态
bool is_wifi_scan_page_shown = false;  // WiFi扫描页面显示状态
bool is_mqtt_page_shown = false;  // MQTT页面显示状态
bool is_mqtt_config_page_shown = false;  // MQTT配置页面显示状态

// MQTT相关变量
lv_obj_t* mqtt_server_ta;    // MQTT服务器输入框
lv_obj_t* mqtt_port_ta;      // MQTT端口输入框
lv_obj_t* mqtt_user_ta;      // MQTT用户名输入框
lv_obj_t* mqtt_password_ta;  // MQTT密码输入框
lv_obj_t* mqtt_topic_ta;     // MQTT主题输入框
lv_obj_t* gpio_btn[3];        // GPIO按钮数组
lv_obj_t* brightness_slider;  // 亮度滑动条

// 手势相关变量
static lv_coord_t gesture_start_x = 0;  // 手势开始位置
static uint32_t gesture_start_time = 0;  // 手势开始时间
static bool gesture_tracking = false;    // 是否正在追踪手势
static uint32_t last_page_switch = 0;   // 上次页面切换时间
const uint32_t PAGE_SWITCH_DEBOUNCE = 500;  // 页面切换防抖时间(ms)
const lv_coord_t GESTURE_THRESHOLD = 50;    // 手势触发阈值

// GPIO相关变量
const int GPIO_PIN_0 = 0;    // GPIO0引脚
const int GPIO_PIN_1 = 1;    // GPIO1引脚
const int GPIO_PIN_2 = 2;    // GPIO2引脚
const int GPIO_PIN_3 = 3;    // GPIO3引脚(PWM亮度控制)
const int PWM_CHANNEL = 0;   // PWM通道
const int PWM_FREQ = 5000;   // PWM频率
const int PWM_RESOLUTION = 8; // PWM分辨率
int current_brightness = 128; // 当前亮度值(0-255)
lv_obj_t* gpio0_label;      // GPIO0状态标签
lv_obj_t* gpio1_label;      // GPIO1状态标签

// 函数声明
void update_temp_humi(lv_timer_t* t);
void update_gpio_status(lv_timer_t* t);
void update_wifi_info();
void wifi_reconnect();
bool mqtt_reconnect();
void publish_gpio_state(int gpio_num);
void publish_brightness();
void update_mqtt_status(lv_timer_t* t);
void create_mqtt_page();
void handle_gesture(lv_event_t * e);

FT6236 ts = FT6236();  // 触摸屏对象
Adafruit_SHT31 sht31 = Adafruit_SHT31();  // 温湿度传感器对象

/* 屏幕分辨率 */
static const uint16_t screenWidth  = 240;  // 屏幕宽度
static const uint16_t screenHeight = 240;  // 屏幕高度

static lv_disp_draw_buf_t draw_buf;  // LVGL显示缓冲区
static lv_color_t buf[screenWidth * 20]; // 进一步增加缓冲区大小

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);  // TFT显示屏对象
bool isTouching = false;  // 触摸状态标志

WiFiUDP ntpUDP;  // UDP对象，用于NTP时间同步
NTPClient timeClient(ntpUDP, "ntp.ntsc.ac.cn", 8 * 3600, 60000);  // NTP客户端，设置时区为东八区

lv_obj_t* time_label;  // 时间标签
lv_obj_t* date_label;  // 日期标签
lv_obj_t* wifi_label;  // WiFi状态标签
lv_obj_t* ip_label;  // IP地址标签
lv_obj_t* temp_label;  // 温度标签
lv_obj_t* humi_label;  // 湿度标签
lv_obj_t* ampm_label;  // AM/PM标签
lv_timer_t* update_timer;  // 定时器对象，用于更新时间

// 启动动画相关变量
lv_obj_t* boot_spinner;     // 加载动画
lv_obj_t* boot_label;       // 启动状态文本

// 触摸按键区域定义
#define BTN1_X 120  // 按键1的X坐标
#define BTN1_Y 300  // 按键1的Y坐标
#define BTN2_X 240  // 按键2的X坐标
#define BTN2_Y 300  // 按键2的Y坐标
#define BTN3_X 400  // 按键3的X坐标
#define BTN3_Y 300  // 按键3的Y坐标
#define BTN_RADIUS 30  // 按键检测半径

// 触摸按键状态
bool btn1_pressed = false;
bool btn2_pressed = false;
bool btn3_pressed = false;

// GPIO状态
bool gpio_states[3] = {false, false, false};  // GPIO 0-2的状态

#if LV_USE_LOG != 0
void my_print(const char * buf)
{
    Serial.print(buf); // 改用更安全的print
}
#endif

// 显示刷新回调函数，用于将LVGL的绘制内容刷新到屏幕上
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}
// 页面切换动画回调函数
static void page_switch_anim_cb(void * var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}
void update_wifi_details() {
    if (!wifi_info_label) return;
    
    String wifi_details = String("SSID: ") + WiFi.SSID() + "\n";
    wifi_details += "IP: " + WiFi.localIP().toString() + "\n";
    wifi_details += "Gateway: " + WiFi.gatewayIP().toString() + "\n";
    wifi_details += "Subnet: " + WiFi.subnetMask().toString() + "\n";
    wifi_details += "DNS: " + WiFi.dnsIP().toString() + "\n";
    wifi_details += "MAC: " + WiFi.macAddress() + "\n";
    wifi_details += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    wifi_details += "Channel: " + String(WiFi.channel()) + "\n";
    
    lv_label_set_text(wifi_info_label, wifi_details.c_str());
}

// 页面枚举
enum PageType {
    PAGE_BOOT,
    PAGE_MAIN,
    PAGE_WIFI,
    PAGE_GPIO,
    PAGE_WIFI_SCAN,
    PAGE_MQTT,
    PAGE_MQTT_CONFIG
};

// 页面管理器类
class PageManager {
private:
    static PageManager* instance;
    PageType currentPage;
    std::map<PageType, lv_obj_t*> pages;
    std::map<PageType, bool> pageStates;

    PageManager() : currentPage(PAGE_BOOT) {}

public:
    static PageManager* getInstance() {
        if (instance == NULL) {
            instance = new PageManager();
        }
        return instance;
    }

    void registerPage(PageType type, lv_obj_t* page) {
        pages[type] = page;
        pageStates[type] = false;
    }

    void switchToPage(PageType targetPage, bool animate = true) {
        if (pages.find(targetPage) == pages.end()) {
            Serial.printf("Error: Target page %d not registered\n", targetPage);
            return;
        }
        
        if (currentPage == targetPage) {
            Serial.printf("Warning: Already on page %d\n", targetPage);
            return;
        }

        Serial.printf("Switching from page %d to page %d\n", currentPage, targetPage);

        // 更新所有页面状态
        for (auto& state : pageStates) {
            state.second = (state.first == targetPage);
            Serial.printf("Page %d active state: %d\n", state.first, state.second);
        }

        if (animate) {
            Serial.println("Using animated transition");
            lv_scr_load_anim(pages[targetPage], LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
        } else {
            Serial.println("Using instant transition");
            lv_scr_load(pages[targetPage]);
        }
        
        currentPage = targetPage;
        Serial.printf("Page switch complete. Current page: %d\n", currentPage);
        
        // 强制更新显示
        lv_timer_handler();
    }

    PageType getCurrentPage() {
        return currentPage;
    }

    PageType getNextPage() {
        switch (currentPage) {
            case PAGE_MAIN:
                return PAGE_GPIO;
            case PAGE_GPIO:
                return PAGE_MQTT;
            case PAGE_MQTT:
                return PAGE_WIFI;
            case PAGE_WIFI:
                return PAGE_WIFI_SCAN;
            case PAGE_WIFI_SCAN:
                return PAGE_MAIN;
            default:
                return PAGE_MAIN;
        }
    }

    PageType getPreviousPage() {
        switch (currentPage) {
            case PAGE_MAIN:
                return PAGE_WIFI_SCAN;
            case PAGE_GPIO:
                return PAGE_MAIN;
            case PAGE_MQTT:
                return PAGE_GPIO;
            case PAGE_WIFI:
                return PAGE_MQTT;
            case PAGE_WIFI_SCAN:
                return PAGE_WIFI;
            default:
                return PAGE_MAIN;
        }
    }

    bool isPageActive(PageType type) {
        return pageStates[type];
    }
};

PageManager* PageManager::instance = NULL;

// 更新时间显示，包括日期和时间
void update_time(lv_timer_t *timer)
{
    static uint32_t last_update = 0;
    uint32_t now = millis();
    
    // 每1000ms更新一次时间
    if (now - last_update < 1000) return;
    last_update = now;

    // 每1小时同步一次时间
    if (now - last_sync_time > 3600000) {
        timeClient.forceUpdate();
        last_sync_time = now;
    }

    timeClient.update();
    int hour = timeClient.getHours();
    int minute = timeClient.getMinutes();
    int second = timeClient.getSeconds();
    
    // 设置背景色
    if (hour >= 8 && hour < 17) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_palette_lighten(LV_PALETTE_BLUE, 3), 0);
    } else {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_palette_darken(LV_PALETTE_BLUE, 4), 0);
    }

    // 格式化时间
    char timeStr[20];
    char ampmStr[3];
    if (use_24h_format) {
        sprintf(timeStr, "%02d:%02d:%02d", hour, minute, second);
        lv_label_set_text(ampm_label, "");  // 清空AM/PM标签
    } else {
        int display_hour = hour % 12;
        if (display_hour == 0) display_hour = 12;
        sprintf(timeStr, "%02d:%02d:%02d", display_hour, minute, second);
        sprintf(ampmStr, "%s", hour >= 12 ? "PM" : "AM");
        lv_label_set_text(ampm_label, ampmStr);  // 设置AM/PM标签
    }

    // 添加日期显示
    if (show_date) {
        char dateStr[20];
        time_t rawtime = timeClient.getEpochTime();
        struct tm * timeinfo = localtime(&rawtime);
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
        lv_label_set_text(date_label, dateStr);
    }
    lv_label_set_text(time_label, timeStr);

    // 更新时间同步状态
    static uint32_t last_sync_check = 0;
    if (now - last_sync_check > 10000) {  // 每10秒检查一次
        last_sync_check = now;
        lv_label_set_text_fmt(wifi_label, "WiFi: %.12s %s", 
            WiFi.SSID().c_str(),
            (now - last_sync_time < 60000) ? "✓" : "⌛");
    }
}

// 更新WiFi信息，包括SSID和IP地址
void update_wifi_info()
{
    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text_fmt(wifi_label, "WiFi: %.12s", WiFi.SSID().c_str());
        lv_label_set_text_fmt(ip_label, "IP: %s", WiFi.localIP().toString().c_str());
    } else {
        lv_label_set_text(wifi_label, "WiFi: Connecting...");
        lv_label_set_text(ip_label, "IP: 0.0.0.0");
    }
}

// WiFi重新连接函数
void wifi_reconnect(){
    if(WiFi.status() != WL_CONNECTED){
        WiFi.reconnect();
        while(WiFi.status() != WL_CONNECTED){
            delay(500);
            Serial.print("...");
        }
        Serial.println("\nReconnected!");
        update_wifi_info();
    }
}


// 更新温湿度显示
void update_temp_humi(lv_timer_t* t)
{
    static uint32_t last_read = 0;
    uint32_t now = millis();
    
    // 限制读取频率，至少间隔2秒
    if (now - last_read < 2000) {
        return;
    }
    last_read = now;

    float temp = sht31.readTemperature();
    float humi = sht31.readHumidity();
    
    if (!isnan(temp) && !isnan(humi)) {
        char tempStr[20];
        char humiStr[20];
        snprintf(tempStr, sizeof(tempStr), "Temp: %.1f°C", temp);
        snprintf(humiStr, sizeof(humiStr), "Humi: %.1f%%", humi);
        if (temp_label && humi_label) {
            lv_label_set_text(temp_label, tempStr);
            lv_label_set_text(humi_label, humiStr);
        }
        Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%\n", temp, humi);
    } else {
        Serial.println("Failed to read temperature or humidity!");
    }
}

// 新增GPIO状态更新函数
void update_gpio_status(lv_timer_t* t)
{
    // 读取GPIO状态
    int gpio0_state = digitalRead(GPIO_PIN_0);
    int gpio1_state = digitalRead(GPIO_PIN_1);

    // 更新GPIO0标签
    char gpio0_str[20];
    snprintf(gpio0_str, sizeof(gpio0_str), "GPIO0: %s", gpio0_state ==1 ? "1" : "0");
    lv_label_set_text(gpio0_label, gpio0_str);

    // 更新GPIO1标签
    char gpio1_str[20];
    snprintf(gpio1_str, sizeof(gpio1_str), "GPIO1: %s", gpio1_state == 1 ? "1" : "0");
    lv_label_set_text(gpio1_label, gpio1_str);
}
// 修改手势处理函数
void handle_gesture(lv_event_t * e) {
    lv_indev_t * indev = lv_indev_get_act();
    if(indev == NULL) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    
    lv_event_code_t code = lv_event_get_code(e);
    
    if(code == LV_EVENT_PRESSED) {
        gesture_tracking = true;
        gesture_start_x = point.x;
        gesture_start_time = millis();
        Serial.printf("Gesture start at x=%d, time=%lu\n", gesture_start_x, gesture_start_time);
    }
    else if(code == LV_EVENT_PRESSING && gesture_tracking) {
        lv_coord_t gesture_distance = point.x - gesture_start_x;
        uint32_t gesture_time = millis() - gesture_start_time;
        Serial.printf("Gesture distance: %d, time: %lu\n", gesture_distance, gesture_time);
        
        // Only trigger if gesture is quick enough (less than 500ms)
        if(abs(gesture_distance) > GESTURE_THRESHOLD && gesture_time < 500) {
            uint32_t now = millis();
            if(now - last_page_switch > PAGE_SWITCH_DEBOUNCE) {
                PageManager* pm = PageManager::getInstance();
                
                if(gesture_distance < 0) {
                    // 左滑
                    Serial.println("Swipe left detected - switching to next page");
                    pm->switchToPage(pm->getNextPage(), true);
                }
                else if(gesture_distance > 0) {
                    // 右滑
                    Serial.println("Swipe right detected - switching to previous page");
                    pm->switchToPage(pm->getPreviousPage(), true);
                }
                last_page_switch = now;
                gesture_tracking = false;
            }
        }
    }
    else if(code == LV_EVENT_CLICKED) {
        // Handle time format switching
        if(lv_obj_check_type(lv_event_get_target(e), &lv_label_class)) {
            static uint32_t last_click_time = 0;
            uint32_t current_time = millis();
            
            // Implement double-click detection
            if(current_time - last_click_time < 300) {
                use_24h_format = !use_24h_format;
                Serial.printf("Time format changed to %s\n", use_24h_format ? "24h" : "12h");
                if(update_timer) {
                    lv_timer_reset(update_timer);
                }
            }
            last_click_time = current_time;
        }
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        gesture_tracking = false;
        Serial.println("Gesture tracking ended");
    }
}
// MQTT回调函数声明
void mqtt_callback(char* topic, byte* payload, unsigned int length);

// MQTT回调函数实现
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    
    String topicStr = String(topic);
    String messageStr = String(message);
    
    Serial.printf("MQTT Message received - Topic: %s, Message: %s\n", topic, message);
    
    // 处理GPIO控制消息
    if (topicStr.endsWith("/gpio0")) {
        Serial.printf("Setting GPIO0 to %s\n", messageStr.c_str());
        digitalWrite(GPIO_PIN_0, messageStr.equals("1") ? HIGH : LOW);
        gpio_states[0] = messageStr.equals("1");
        update_gpio_status(NULL);
    }
    else if (topicStr.endsWith("/gpio1")) {
        Serial.printf("Setting GPIO1 to %s\n", messageStr.c_str());
        digitalWrite(GPIO_PIN_1, messageStr.equals("1") ? HIGH : LOW);
        gpio_states[1] = messageStr.equals("1");
        update_gpio_status(NULL);
    }
    else if (topicStr.endsWith("/gpio2")) {
        Serial.printf("Setting GPIO2 to %s\n", messageStr.c_str());
        digitalWrite(GPIO_PIN_2, messageStr.equals("1") ? HIGH : LOW);
        gpio_states[2] = messageStr.equals("1");
        update_gpio_status(NULL);
    }
    // 处理亮度控制消息
    else if (topicStr.endsWith("/brightness")) {
        Serial.printf("Setting brightness to %s\n", messageStr.c_str());
        current_brightness = messageStr.toInt();
        if (current_brightness < 0) current_brightness = 0;
        if (current_brightness > 255) current_brightness = 255;
        ledcWrite(PWM_CHANNEL, current_brightness);
        if (brightness_slider) {
            lv_slider_set_value(brightness_slider, current_brightness, LV_ANIM_ON);
        }
    }
}


// 创建WiFi信息页面
void create_wifi_page() {
    wifi_page = lv_obj_create(NULL);
    lv_obj_set_size(wifi_page, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(wifi_page, lv_color_white(), 0);
    
    // 创建标题
    lv_obj_t* title = lv_label_create(wifi_page);
    lv_label_set_text(title, "WiFi Information");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建WiFi信息标签
    wifi_info_label = lv_label_create(wifi_page);
    lv_obj_set_style_text_font(wifi_info_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_info_label, lv_color_black(), 0);
    lv_label_set_text(wifi_info_label, "Loading WiFi info...");
    lv_obj_align(wifi_info_label, LV_ALIGN_TOP_LEFT, 10, 50);
    
    // 创建返回提示
    lv_obj_t* hint = lv_label_create(wifi_page);
    lv_label_set_text(hint, "← Swipe to navigate →");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // 创建手势检测区域
    static lv_style_t style_trans;
    lv_style_init(&style_trans);
    lv_style_set_bg_opa(&style_trans, LV_OPA_TRANSP);
    
    // 创建一个覆盖整个页面的手势检测对象
    lv_obj_t* gesture_obj = lv_obj_create(wifi_page);
    lv_obj_remove_style_all(gesture_obj);
    lv_obj_add_style(gesture_obj, &style_trans, 0);
    lv_obj_set_size(gesture_obj, screenWidth, screenHeight);
    lv_obj_align(gesture_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gesture_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(gesture_obj, LV_OPA_0, 0);
    
    // 将手势对象置于最顶层
    lv_obj_move_foreground(gesture_obj);
    
    // 添加手势事件回调
    lv_obj_add_event_cb(gesture_obj, [](lv_event_t * e) {
        handle_gesture(e);
    }, LV_EVENT_ALL, NULL);
}
// 更新启动状态
void update_boot_status(const char* message) {
    if (boot_label) {
        lv_label_set_text(boot_label, message);
    }
}
// 创建GPIO状态页面
void create_gpio_page() {
    gpio_page = lv_obj_create(NULL);
    lv_obj_set_size(gpio_page, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(gpio_page, lv_color_white(), 0);
    
    // 创建标题
    lv_obj_t* title = lv_label_create(gpio_page);
    lv_label_set_text(title, "GPIO Status");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建GPIO状态标签
    gpio0_label = lv_label_create(gpio_page);
    lv_label_set_text(gpio0_label, "GPIO0: --");
    lv_obj_set_style_text_font(gpio0_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(gpio0_label, lv_color_black(), 0);
    lv_obj_align(gpio0_label, LV_ALIGN_CENTER, 0, -30);

    gpio1_label = lv_label_create(gpio_page);
    lv_label_set_text(gpio1_label, "GPIO1: --");
    lv_obj_set_style_text_font(gpio1_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(gpio1_label, lv_color_black(), 0);
    lv_obj_align(gpio1_label, LV_ALIGN_CENTER, 0, 30);
    
    // 创建导航提示
    lv_obj_t* hint = lv_label_create(gpio_page);
    lv_label_set_text(hint, "← Swipe to navigate →");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // 创建手势检测区域
    static lv_style_t style_trans;
    lv_style_init(&style_trans);
    lv_style_set_bg_opa(&style_trans, LV_OPA_TRANSP);
    
    // 创建一个覆盖整个页面的手势检测对象
    lv_obj_t* gesture_obj = lv_obj_create(gpio_page);
    lv_obj_remove_style_all(gesture_obj);
    lv_obj_add_style(gesture_obj, &style_trans, 0);
    lv_obj_set_size(gesture_obj, screenWidth, screenHeight);
    lv_obj_align(gesture_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gesture_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(gesture_obj, LV_OPA_0, 0);
    
    // 将手势对象置于最顶层
    lv_obj_move_foreground(gesture_obj);
    
    // 添加手势事件回调
    lv_obj_add_event_cb(gesture_obj, [](lv_event_t * e) {
        handle_gesture(e);
    }, LV_EVENT_ALL, NULL);
}
// 创建启动页面
void create_boot_page(const char* message = "System Starting...") {
    boot_page = lv_obj_create(NULL);
    lv_obj_set_size(boot_page, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(boot_page, lv_color_black(), 0);
    
    // 创建标题
    lv_obj_t* title = lv_label_create(boot_page);
    lv_label_set_text(title, "Smart Clock");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    // 创建加载动画
    boot_spinner = lv_spinner_create(boot_page, 1000, 60);
    lv_obj_set_size(boot_spinner, 100, 100);
    lv_obj_align(boot_spinner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_color(boot_spinner, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(boot_spinner, lv_palette_darken(LV_PALETTE_BLUE, 2), LV_PART_MAIN);
    
    // 创建状态文本
    boot_label = lv_label_create(boot_page);
    lv_label_set_text(boot_label, message);
    lv_obj_set_style_text_font(boot_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(boot_label, lv_color_white(), 0);
    lv_obj_align(boot_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    
    // 加载启动页面
    lv_scr_load(boot_page);
}
// 修改WiFi信息更新定时器回调
void update_wifi_info_timer(lv_timer_t* t) {
    PageManager* pm = PageManager::getInstance();
    if (pm->isPageActive(PAGE_WIFI)) {
        update_wifi_details();
    }
}
// WiFi扫描回调函数
static void scan_wifi_cb(lv_event_t * e) {
    Serial.println("Starting WiFi scan...");
    
    // 检查WiFi模式
    Serial.printf("Current WiFi mode: %d (1=STA, 2=AP, 3=APSTA)\n", WiFi.getMode());
    
    // 开始扫描
    Serial.println("Scanning networks...");
    int n = WiFi.scanNetworks();
    Serial.printf("Scan completed. Found %d networks\n", n);
    
    String wifi_list = "";
    
    if (n == 0) {
        wifi_list = "No networks found\n";
        Serial.println("No networks found");
    } else {
        wifi_list = String(n) + " networks found:\n\n";
        Serial.printf("%d networks found:\n", n);
        
        for (int i = 0; i < n; ++i) {
            // 获取网络信息
            String ssid = WiFi.SSID(i);
            int32_t rssi = WiFi.RSSI(i);
            wifi_auth_mode_t encType = WiFi.encryptionType(i);
            
            // 打印调试信息
            Serial.printf("Network %d:\n", i + 1);
            Serial.printf("  SSID: %s\n", ssid.c_str());
            Serial.printf("  RSSI: %d dBm\n", rssi);
            Serial.printf("  Encryption: %d\n", encType);
            
            // 构建显示文本
            wifi_list += String(i + 1) + ": ";
            wifi_list += ssid + " (";
            wifi_list += String(rssi) + "dBm) ";
            wifi_list += (encType == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
            wifi_list += "\n";
        }
    }
    
    // 更新UI前打印将要显示的内容
    Serial.println("Updating WiFi list label with text:");
    Serial.println(wifi_list);
    
    // 检查标签对象是否存在
    if (wifi_list_label == NULL) {
        Serial.println("ERROR: wifi_list_label is NULL!");
        return;
    }
    
    // 更新UI
    lv_label_set_text(wifi_list_label, wifi_list.c_str());
    Serial.println("WiFi list label updated");
    
    // 强制刷新显示
    lv_obj_invalidate(wifi_list_label);
    Serial.println("Display invalidated for refresh");
}
// MQTT重连函数
bool mqtt_reconnect() {
    if (!mqtt_client.connected() && strlen(mqtt_server) > 0) {
        Serial.printf("Attempting MQTT connection to %s:%s...\n", mqtt_server, mqtt_port);
        String clientId = "ESP32Client-";
        clientId += String(random(0xffff), HEX);
        Serial.printf("Using client ID: %s\n", clientId.c_str());
        
        if (mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
            Serial.println("MQTT Connected successfully!");
            mqtt_connected = true;
            
            // 订阅主题
            String topic_prefix = String(mqtt_topic_prefix);
            Serial.println("Subscribing to topics:");
            
            String topics[] = {"/gpio0", "/gpio1", "/gpio2", "/brightness"};
            for (const String& topic : topics) {
                String full_topic = topic_prefix + topic;
                Serial.printf("  Subscribing to: %s\n", full_topic.c_str());
                mqtt_client.subscribe(full_topic.c_str());
            }
            
            // 发布当前状态
            Serial.println("Publishing current states:");
            for (int i = 0; i < 3; i++) {
                String topic = topic_prefix + "/gpio" + String(i);
                String state = gpio_states[i] ? "1" : "0";
                Serial.printf("  GPIO%d -> %s: %s\n", i, topic.c_str(), state.c_str());
                mqtt_client.publish(topic.c_str(), state.c_str());
            }
            
            String bright_topic = topic_prefix + "/brightness";
            Serial.printf("  Brightness -> %s: %d\n", bright_topic.c_str(), current_brightness);
            mqtt_client.publish(bright_topic.c_str(), String(current_brightness).c_str());
            
            return true;
        } else {
            Serial.printf("MQTT connection failed, rc=%d\n", mqtt_client.state());
            Serial.println("Will try again in next update cycle");
            mqtt_connected = false;
        }
    }
    return mqtt_client.connected();
}

// 发布GPIO状态
void publish_gpio_state(int gpio_num) {
    if (mqtt_connected && gpio_num >= 0 && gpio_num < 3) {
        String topic = String(mqtt_topic_prefix) + mqtt_topic_gpio + String(gpio_num);
        mqtt_client.publish(topic.c_str(), gpio_states[gpio_num] ? "1" : "0");
    }
}
// 创建WiFi扫描页面
void create_wifi_scan_page() {
    wifi_scan_page = lv_obj_create(NULL);
    lv_obj_set_size(wifi_scan_page, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(wifi_scan_page, lv_color_black(), 0);
    
    // 创建标题
    lv_obj_t* title = lv_label_create(wifi_scan_page);
    lv_label_set_text(title, "WiFi Scanner");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建一个容器来放置WiFi列表
    lv_obj_t* list_cont = lv_obj_create(wifi_scan_page);
    lv_obj_set_size(list_cont, screenWidth - 20, screenHeight - 100);
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(list_cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 5, 0);
    
    // 创建WiFi列表标签
    wifi_list_label = lv_label_create(list_cont);
    lv_obj_set_width(wifi_list_label, screenWidth - 30);
    lv_obj_set_style_text_font(wifi_list_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_list_label, lv_color_white(), 0);
    lv_label_set_text(wifi_list_label, "Press SCAN to search for networks...");
    lv_label_set_long_mode(wifi_list_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(wifi_list_label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // 创建按钮样式
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn, 2);
    lv_style_set_border_color(&style_btn, lv_color_white());
    lv_style_set_shadow_width(&style_btn, 5);
    lv_style_set_shadow_color(&style_btn, lv_color_white());
    lv_style_set_shadow_opa(&style_btn, LV_OPA_50);
    lv_style_set_pad_all(&style_btn, 5);
    
    // 创建扫描按钮
    lv_obj_t* scan_btn = lv_btn_create(wifi_scan_page);
    lv_obj_set_size(scan_btn, 80, 40);
    lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_flag(scan_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scan_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_style(scan_btn, &style_btn, 0);
    
    // 创建扫描按钮标签
    lv_obj_t* scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "SCAN");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(scan_label, lv_color_white(), 0);
    lv_obj_center(scan_label);
    
    // 创建重置按钮
    lv_obj_t* reset_btn = lv_btn_create(wifi_scan_page);
    lv_obj_set_size(reset_btn, 80, 40);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_flag(reset_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(reset_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_style(reset_btn, &style_btn, 0);
    lv_obj_set_style_bg_color(reset_btn, lv_palette_main(LV_PALETTE_RED), 0);
    
    // 创建重置按钮标签
    lv_obj_t* reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "RESET");
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(reset_label, lv_color_white(), 0);
    lv_obj_center(reset_label);
    
    // 创建状态标签（用于显示按钮按下状态）
    lv_obj_t* status_label = lv_label_create(wifi_scan_page);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_label_set_text(status_label, "");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    
    // 创建手势检测区域
    static lv_style_t style_gesture;
    lv_style_init(&style_gesture);
    lv_style_set_bg_opa(&style_gesture, LV_OPA_TRANSP);
    
    // 创建一个覆盖页面上部的手势检测对象（避开按钮区域）
    lv_obj_t* gesture_obj = lv_obj_create(wifi_scan_page);
    lv_obj_remove_style_all(gesture_obj);
    lv_obj_add_style(gesture_obj, &style_gesture, 0);
    lv_obj_set_size(gesture_obj, screenWidth, screenHeight - 70);
    lv_obj_align(gesture_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gesture_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(gesture_obj, LV_OPA_0, 0);
    
    // 将手势对象置于列表容器之后，按钮之前
    lv_obj_move_background(gesture_obj);
    
    // 添加手势事件回调
    lv_obj_add_event_cb(gesture_obj, [](lv_event_t * e) {
        handle_gesture(e);
    }, LV_EVENT_ALL, NULL);
    
    // 创建导航提示
    lv_obj_t* hint = lv_label_create(wifi_scan_page);
    lv_label_set_text(hint, "← Swipe right");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_white(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -35);
    
    // 添加扫描按钮事件回调
    lv_obj_add_event_cb(scan_btn, [](lv_event_t * e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* parent = lv_obj_get_parent(btn);
        lv_obj_t* status_label = lv_obj_get_child(parent, -3);
        
        if(code == LV_EVENT_CLICKED) {
            Serial.println("Scan button clicked!");
            lv_label_set_text(status_label, "Scanning...");
            scan_wifi_cb(e);
            lv_timer_handler();
        }
    }, LV_EVENT_ALL, NULL);
    
    // 添加重置按钮事件回调
    lv_obj_add_event_cb(reset_btn, [](lv_event_t * e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* parent = lv_obj_get_parent(btn);
        lv_obj_t* status_label = lv_obj_get_child(parent, -3);
        
        if(code == LV_EVENT_CLICKED) {
            Serial.println("Reset button clicked!");
            lv_label_set_text(status_label, "Resetting WiFi...");
            lv_timer_handler();
            
            // 显示重置页面
            create_boot_page("Resetting WiFi Settings...");
            lv_timer_handler();
            delay(500);
            
            WiFiManager wm;
            wm.resetSettings();
            
            update_boot_status("Restarting system...");
            lv_timer_handler();
            delay(1000);
            ESP.restart();
        }
    }, LV_EVENT_ALL, NULL);
}

// 创建MQTT页面
void create_mqtt_page() {
    mqtt_page = lv_obj_create(NULL);
    lv_obj_set_size(mqtt_page, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(mqtt_page, lv_color_black(), 0);
    
    // 创建标题
    lv_obj_t* title = lv_label_create(mqtt_page);
    lv_label_set_text(title, "MQTT Control");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建GPIO按钮
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_border_width(&style_btn, 2);
    lv_style_set_border_color(&style_btn, lv_color_white());
    
    for(int i = 0; i < 3; i++) {
        gpio_btn[i] = lv_btn_create(mqtt_page);
        lv_obj_set_size(gpio_btn[i], 60, 40);
        lv_obj_align(gpio_btn[i], LV_ALIGN_TOP_LEFT, 20 + i * 80, 60);
        lv_obj_add_style(gpio_btn[i], &style_btn, 0);
        
        lv_obj_t* label = lv_label_create(gpio_btn[i]);
        lv_label_set_text_fmt(label, "GPIO%d", i);
        lv_obj_center(label);
        
        // 添加按钮事件
        lv_obj_add_event_cb(gpio_btn[i], [](lv_event_t * e) {
            lv_event_code_t code = lv_event_get_code(e);
            lv_obj_t* btn = lv_event_get_target(e);
            
            if(code == LV_EVENT_CLICKED) {
                for(int j = 0; j < 3; j++) {
                    if(btn == gpio_btn[j]) {
                        gpio_states[j] = !gpio_states[j];
                        digitalWrite(j, gpio_states[j] ? HIGH : LOW);
                        Serial.printf("GPIO%d button clicked, new state: %d\n", j, gpio_states[j]);
                        lv_obj_set_style_bg_color(btn, 
                            gpio_states[j] ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
                        publish_gpio_state(j);
                        break;
                    }
                }
            }
        }, LV_EVENT_ALL, NULL);
    }
    
    // 创建亮度滑动条
    static lv_style_t style_slider;
    lv_style_init(&style_slider);
    lv_style_set_bg_color(&style_slider, lv_color_white());
    lv_style_set_border_color(&style_slider, lv_color_white());
    
    brightness_slider = lv_slider_create(mqtt_page);
    lv_obj_set_size(brightness_slider, 200, 10);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_add_style(brightness_slider, &style_slider, LV_PART_MAIN);
    lv_obj_add_style(brightness_slider, &style_slider, LV_PART_INDICATOR);
    lv_obj_add_style(brightness_slider, &style_slider, LV_PART_KNOB);
    lv_slider_set_range(brightness_slider, 0, 255);
    lv_slider_set_value(brightness_slider, current_brightness, LV_ANIM_OFF);
    
    // 创建亮度标签
    lv_obj_t* bright_label = lv_label_create(mqtt_page);
    lv_label_set_text_fmt(bright_label, "Brightness: %d", current_brightness);
    lv_obj_set_style_text_color(bright_label, lv_color_white(), 0);
    lv_obj_align_to(bright_label, brightness_slider, LV_ALIGN_OUT_TOP_MID, 0, -10);
    
    // 添加滑动条事件
    lv_obj_add_event_cb(brightness_slider, [](lv_event_t * e) {
        lv_obj_t* slider = lv_event_get_target(e);
        lv_obj_t* label = lv_obj_get_child(lv_obj_get_parent(slider), -4); // 获取亮度标签
        
        if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
            current_brightness = lv_slider_get_value(slider);
            Serial.printf("Brightness slider changed to: %d\n", current_brightness);
            ledcWrite(PWM_CHANNEL, current_brightness);
            lv_label_set_text_fmt(label, "Brightness: %d", current_brightness);
            publish_brightness();
        }
    }, LV_EVENT_ALL, NULL);
    
    // 创建MQTT状态标签
    mqtt_info_label = lv_label_create(mqtt_page);
    lv_obj_set_style_text_font(mqtt_info_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mqtt_info_label, lv_color_white(), 0);
    lv_label_set_text_fmt(mqtt_info_label, "MQTT: %s\nServer: %s:%s", 
        mqtt_connected ? "Connected" : "Disconnected",
        mqtt_server, mqtt_port);
    lv_obj_align(mqtt_info_label, LV_ALIGN_TOP_LEFT, 10, 160);
    
    // 创建配置按钮
    lv_obj_t* config_btn = lv_btn_create(mqtt_page);
    lv_obj_set_size(config_btn, 100, 40);
    lv_obj_align(config_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_obj_t* config_label = lv_label_create(config_btn);
    lv_label_set_text(config_label, "Config");
    lv_obj_center(config_label);
    
    // 添加配置按钮事件
    lv_obj_add_event_cb(config_btn, [](lv_event_t * e) {
        if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
            WiFiManager wm;
            
            // 添加MQTT配置参数
            WiFiManagerParameter custom_mqtt_server("server", "MQTT server", mqtt_server, 40);
            WiFiManagerParameter custom_mqtt_port("port", "MQTT port", mqtt_port, 6);
            WiFiManagerParameter custom_mqtt_user("user", "MQTT user", mqtt_user, 32);
            WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", mqtt_password, 32);
            WiFiManagerParameter custom_mqtt_topic("topic", "MQTT topic prefix", mqtt_topic_prefix, 32);
            
            wm.addParameter(&custom_mqtt_server);
            wm.addParameter(&custom_mqtt_port);
            wm.addParameter(&custom_mqtt_user);
            wm.addParameter(&custom_mqtt_pass);
            wm.addParameter(&custom_mqtt_topic);
            
            wm.startConfigPortal("SmartClockAP");
            
            // 保存MQTT配置
            strcpy(mqtt_server, custom_mqtt_server.getValue());
            strcpy(mqtt_port, custom_mqtt_port.getValue());
            strcpy(mqtt_user, custom_mqtt_user.getValue());
            strcpy(mqtt_password, custom_mqtt_pass.getValue());
            strcpy(mqtt_topic_prefix, custom_mqtt_topic.getValue());
            
            // 重新初始化MQTT客户端
            mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
            mqtt_client.setCallback(mqtt_callback);
            
            ESP.restart();
        }
    }, LV_EVENT_ALL, NULL);
    
    // 创建手势检测区域
    static lv_style_t style_trans;
    lv_style_init(&style_trans);
    lv_style_set_bg_opa(&style_trans, LV_OPA_TRANSP);
    
    lv_obj_t* gesture_obj = lv_obj_create(mqtt_page);
    lv_obj_remove_style_all(gesture_obj);
    lv_obj_add_style(gesture_obj, &style_trans, 0);
    lv_obj_set_size(gesture_obj, screenWidth, screenHeight - 80);
    lv_obj_align(gesture_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gesture_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    
    // 添加手势事件回调
    lv_obj_add_event_cb(gesture_obj, [](lv_event_t * e) {
        handle_gesture(e);
    }, LV_EVENT_ALL, NULL);
}


// 触摸屏读取回调函数
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    // 检查是否有触摸事件
    if (!ts.touched()) {
        data->state = LV_INDEV_STATE_REL;  // 释放状态
        isTouching = false;
        
        // 重置所有按键状态
        btn1_pressed = false;
        btn2_pressed = false;
        btn3_pressed = false;
        return;
    }
    
    data->state = LV_INDEV_STATE_PR;  // 按下状态
    isTouching = true;
    
    // 获取触摸点坐标
    TS_Point p = ts.getPoint();
    
    // 将触摸坐标映射到屏幕坐标
    data->point.x = p.x;
    data->point.y = p.y;
    
    // 检测按键1是否被按下
    if (abs(p.x - BTN1_X) < BTN_RADIUS && abs(p.y - BTN1_Y) < BTN_RADIUS) {
        if (!btn1_pressed) {
            btn1_pressed = true;
            Serial.println("按键1被按下");
            // 在这里添加按键1的处理逻辑
            PageManager* pm = PageManager::getInstance();
            pm->switchToPage(PAGE_WIFI, true);
        }
    }
    
    // 检测按键2是否被按下
    if (abs(p.x - BTN2_X) < BTN_RADIUS && abs(p.y - BTN2_Y) < BTN_RADIUS) {
        if (!btn2_pressed) {
            btn2_pressed = true;
            Serial.println("按键2被按下");
            // 在这里添加按键2的处理逻辑
            PageManager* pm = PageManager::getInstance();
            pm->switchToPage(PAGE_MAIN, true);
        }
    }
    
    // 检测按键3是否被按下
    if (abs(p.x - BTN3_X) < BTN_RADIUS && abs(p.y - BTN3_Y) < BTN_RADIUS) {
        if (!btn3_pressed) {
            btn3_pressed = true;
            Serial.println("按键3被按下");
            // 在这里添加按键3的处理逻辑
            PageManager* pm = PageManager::getInstance();
            pm->switchToPage(PAGE_GPIO, true);
        }
    }
    
    Serial.printf("触摸坐标: raw(%d, %d) -> mapped(%d, %d)\n", 
                 p.x, p.y, data->point.x, data->point.y);
}
// 发布亮度状态
void publish_brightness() {
    if (mqtt_connected) {
        String topic = String(mqtt_topic_prefix) + mqtt_topic_brightness;
        mqtt_client.publish(topic.c_str(), String(current_brightness).c_str());
    }
}

// MQTT状态更新定时器回调
void update_mqtt_status(lv_timer_t* t) {
    static uint32_t last_status_print = 0;
    uint32_t now = millis();
    
    // 每10秒打印一次MQTT状态
    if (now - last_status_print > 10000) {
        Serial.printf("MQTT Status - Connected: %s, Server: %s:%s\n", 
            mqtt_connected ? "Yes" : "No",
            mqtt_server, mqtt_port);
        last_status_print = now;
    }
    
    if (mqtt_info_label) {
        lv_label_set_text_fmt(mqtt_info_label, "MQTT: %s\nServer: %s:%s", 
            mqtt_connected ? "Connected" : "Disconnected",
            mqtt_server, mqtt_port);
    }
    
    // 如果MQTT断开连接，尝试重连
    if (!mqtt_client.connected()) {
        Serial.println("MQTT disconnected, attempting reconnection...");
        mqtt_connected = mqtt_reconnect();
        if (mqtt_connected) {
            Serial.println("MQTT reconnection successful");
        } else {
            Serial.println("MQTT reconnection failed");
        }
    }
    
    // 如果连接正常，处理MQTT消息
    if (mqtt_connected) {
        mqtt_client.loop();
    }
}
// 创建主页面
void create_main_page() {
    main_page = lv_obj_create(NULL);
    lv_obj_set_size(main_page, screenWidth, screenHeight);
    lv_obj_set_style_bg_color(main_page, lv_palette_darken(LV_PALETTE_BLUE, 4), 0);

    // 创建手势检测区域
    static lv_style_t style_trans;
    lv_style_init(&style_trans);
    lv_style_set_bg_opa(&style_trans, LV_OPA_TRANSP);
    
    lv_obj_t* gesture_obj = lv_obj_create(main_page);
    lv_obj_remove_style_all(gesture_obj);
    lv_obj_add_style(gesture_obj, &style_trans, 0);
    lv_obj_set_size(gesture_obj, screenWidth, screenHeight);
    lv_obj_align(gesture_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(gesture_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(gesture_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(gesture_obj, LV_OPA_0, 0);
    
    lv_obj_move_foreground(gesture_obj);
    
    lv_obj_add_event_cb(gesture_obj, [](lv_event_t * e) {
        handle_gesture(e);
    }, LV_EVENT_ALL, NULL);
}
// 创建时间标签
lv_obj_t* create_time_label() {
    // 设置深色主题
    lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                         LV_THEME_DEFAULT_DARK, &lv_font_montserrat_16);

    // 创建日期标签
    lv_obj_t* date_label = lv_label_create(main_page);
    lv_label_set_text(date_label, "2025-03-24");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(date_label, LV_OPA_TRANSP, 0);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 20);

    // 创建时间标签
    lv_obj_t* label = lv_label_create(main_page);
    lv_label_set_text(label, "00:00:00");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    time_label = label;
    
    // 添加渐变背景
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_grad_dir(&style, LV_GRAD_DIR_VER);
    lv_style_set_bg_grad_color(&style, lv_palette_darken(LV_PALETTE_BLUE, 2));
    lv_style_set_bg_main_stop(&style, 0);
    lv_style_set_bg_grad_stop(&style, 255);
    lv_obj_add_style(label, &style, 0);
    
    // 添加阴影效果
    lv_obj_set_style_shadow_width(label, 20, 0);
    lv_obj_set_style_shadow_ofs_x(label, 5, 0);
    lv_obj_set_style_shadow_ofs_y(label, 5, 0);
    lv_obj_set_style_shadow_color(label, lv_palette_darken(LV_PALETTE_BLUE, 4), 0);
    
    lv_obj_align_to(label, date_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // 添加点击事件
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);  // 确保标签可点击
    lv_obj_add_event_cb(label, [](lv_event_t * e) {
        static uint32_t last_click_time = 0;
        uint32_t current_time = millis();
        
        if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
            Serial.println("Time label clicked");
            
            // 检测双击（300ms内的两次点击）
            if(current_time - last_click_time < 300) {
                use_24h_format = !use_24h_format;
                Serial.printf("Time format changed to %s\n", use_24h_format ? "24h" : "12h");
                if(update_timer) {
                    lv_timer_reset(update_timer);
                }
            }
            last_click_time = current_time;
        }
    }, LV_EVENT_ALL, NULL);
    
    // 创建温度标签
    temp_label = lv_label_create(main_page);
    lv_label_set_text(temp_label, "Temp: --°C");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
    lv_obj_align_to(temp_label, time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);

    // 创建湿度标签
    humi_label = lv_label_create(main_page);
    lv_label_set_text(humi_label, "Humi: --%");
    lv_obj_set_style_text_font(humi_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(humi_label, lv_color_white(), 0);
    lv_obj_align_to(humi_label, temp_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 创建AM/PM标签
    ampm_label = lv_label_create(main_page);
    lv_label_set_text(ampm_label, "");
    lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ampm_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ampm_label, LV_OPA_TRANSP, 0);
    lv_obj_align_to(ampm_label, time_label, LV_ALIGN_OUT_RIGHT_MID, -20, 20);
    
    // 添加触摸事件
    lv_obj_add_event_cb(label, [](lv_event_t * e) {
        static uint32_t last_tap = 0;
        uint32_t now = millis();
        
        if (now - last_tap < 500) return;
        last_tap = now;
        
        use_24h_format = !use_24h_format;
        Serial.printf("Time format changed to %s\n", use_24h_format ? "24h" : "12h");
        
        if (lv_event_get_param(e) && lv_indev_get_scroll_obj(lv_event_get_indev(e))) {
            show_date = !show_date;
            Serial.printf("Date display %s\n", show_date ? "enabled" : "disabled");
        }
        
        lv_timer_reset(update_timer);
        Serial.println("Time display updated");
    }, LV_EVENT_CLICKED, NULL);
    
    Serial.println("Time label created with touch support");
    return date_label;
}

// 创建状态栏
lv_obj_t* create_status_bar() {
    lv_obj_t* status_bar = lv_obj_create(main_page);
    lv_obj_set_size(status_bar, screenWidth, 30);
    
    static lv_style_t status_style;
    lv_style_init(&status_style);
    lv_style_set_bg_opa(&status_style, LV_OPA_COVER);
    lv_style_set_bg_color(&status_style, lv_palette_darken(LV_PALETTE_BLUE, 3));
    lv_style_set_shadow_width(&status_style, 10);
    lv_style_set_shadow_ofs_x(&status_style, 0);
    lv_style_set_shadow_ofs_y(&status_style, 2);
    lv_style_set_shadow_color(&status_style, lv_palette_darken(LV_PALETTE_BLUE, 4));
    lv_obj_add_style(status_bar, &status_style, 0);
    
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    wifi_label = lv_label_create(status_bar);
    lv_label_set_text(wifi_label, "");
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, -10, 0);

    ip_label = lv_label_create(status_bar);
    lv_label_set_text(ip_label, "");
    lv_obj_set_style_text_color(ip_label, lv_color_white(), 0);
    lv_obj_align(ip_label, LV_ALIGN_RIGHT_MID, 0, 0);

    return status_bar;
}

// 初始化页面管理器
void init_page_manager() {
    PageManager* pm = PageManager::getInstance();
    
    // 注册所有页面
    pm->registerPage(PAGE_BOOT, boot_page);
    pm->registerPage(PAGE_MAIN, main_page);
    pm->registerPage(PAGE_WIFI, wifi_page);
    pm->registerPage(PAGE_GPIO, gpio_page);
    pm->registerPage(PAGE_WIFI_SCAN, wifi_scan_page);
    pm->registerPage(PAGE_MQTT, mqtt_page);  // 注册MQTT页面
    
    // 设置初始页面为启动页面
    pm->switchToPage(PAGE_BOOT, false);
    
    // 确保页面切换生效
    lv_timer_handler();
    
    Serial.println("Page manager initialized");
}



// 初始化函数
void setup()
{
    Serial.begin(115200);
    delay(100); // 等待串口稳定

    // 初始化I2C总线
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);  // 设置I2C时钟频率为100kHz
    delay(100); // 等待I2C总线稳定

    // 初始化显示相关
    lv_init();
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // 初始化显示缓冲区
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 20);

    // 初始化显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // 立即创建并显示启动页面
    create_boot_page("Starting system...");
    lv_timer_handler();  // 强制更新显示

    // 初始化SHT30
    update_boot_status("Initializing sensors...");
    lv_timer_handler();
    
    // 初始化SHT30传感器
    bool sht31_ok = false;
    if (!sht31.begin(0x45)) {  // 使用正确的地址0x45
        Serial.println("Could not find SHT31 sensor!");
        update_boot_status("SHT30 sensor not found!");
        lv_timer_handler();
        delay(1000);
    } else {
        Serial.println("SHT31 sensor initialized successfully!");
        delay(100); // 等待传感器稳定
        // 立即读取一次温湿度，测试传感器
        float temp = sht31.readTemperature();
        float humi = sht31.readHumidity();
        if (!isnan(temp) && !isnan(humi)) {
            sht31_ok = true;
            Serial.printf("Initial reading - Temperature: %.2f°C, Humidity: %.2f%%\n", temp, humi);
        } else {
            Serial.println("SHT31 sensor not working properly");
        }
    }

    // 初始化触摸屏
    update_boot_status("Initializing touch screen...");
    lv_timer_handler();
    if (!ts.begin(TOUCH_SENSITIVITY, I2C_SDA, I2C_SCL)) {
        update_boot_status("Touch screen init failed!");
        lv_timer_handler();
        delay(2000);
    }

    // 初始化触摸驱动
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // 初始化GPIO引脚
    pinMode(GPIO_PIN_0, INPUT_PULLDOWN);
    pinMode(GPIO_PIN_1, INPUT_PULLDOWN);

#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    // WiFi连接
    update_boot_status("Connecting to WiFi...");
    lv_timer_handler();
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    
    // 添加MQTT配置参数
    WiFiManagerParameter custom_mqtt_server("server", "MQTT server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "MQTT port", mqtt_port, 6);
    WiFiManagerParameter custom_mqtt_user("user", "MQTT user", mqtt_user, 32);
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", mqtt_password, 32);
    WiFiManagerParameter custom_mqtt_topic("topic", "MQTT topic prefix", mqtt_topic_prefix, 32);
    
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_pass);
    wm.addParameter(&custom_mqtt_topic);
    
    bool res = wm.autoConnect("SmartClockAP");
    
    if (!res) {
        update_boot_status("Failed to connect WiFi!");
        lv_timer_handler();
        delay(2000);
        ESP.restart();
    }
    
    // 保存MQTT配置
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_user, custom_mqtt_user.getValue());
    strcpy(mqtt_password, custom_mqtt_pass.getValue());
    strcpy(mqtt_topic_prefix, custom_mqtt_topic.getValue());
    
    // 初始化MQTT客户端
    mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
    mqtt_client.setCallback(mqtt_callback);
    
    update_boot_status("WiFi connected successfully!");
    lv_timer_handler();

    // 时间服务
    update_boot_status("Syncing time...");
    lv_timer_handler();
    timeClient.begin();
    timeClient.forceUpdate();

    // 创建所有页面
    update_boot_status("Creating interface...");
    lv_timer_handler();

    // 创建所有页面
    create_boot_page("Starting system...");
    create_main_page();
    create_wifi_page();
    create_gpio_page();
    create_wifi_scan_page();
    create_mqtt_page();

    // 初始化页面管理器
    init_page_manager();

    // 创建界面元素
    date_label = create_time_label();
    create_status_bar();
    update_wifi_info();
    
    // 启动完成，切换到主页面
    update_boot_status("Starting system...");
    lv_timer_handler();
    delay(500);
    
    // 使用页面管理器切换到主页面
    PageManager::getInstance()->switchToPage(PAGE_MAIN, true);
    lv_timer_handler();  // 确保切换生效

    // 创建定时器
    update_timer = lv_timer_create(update_time, 1000, NULL);
    lv_timer_set_repeat_count(update_timer, LV_ANIM_REPEAT_INFINITE);
    lv_timer_ready(update_timer);

    // 创建GPIO状态更新定时器（每500ms更新一次）
    lv_timer_create(update_gpio_status, 500, NULL);

    // 创建WiFi信息更新定时器（每5秒更新一次）
    lv_timer_create(update_wifi_info_timer, 5000, NULL);

    // 创建MQTT状态更新定时器（每5秒更新一次）
    lv_timer_create(update_mqtt_status, 5000, NULL);

    // 如果温湿度传感器正常，创建温湿度更新定时器
    if (sht31_ok) {
        lv_timer_create(update_temp_humi, 5000, NULL);
        Serial.println("Temperature and humidity timer created");
    }
}

// 主循环函数
void loop()
{
    lv_task_handler();  // 处理LVGL任务
    if (!isTouching) {
        delay(20);
    }
    else {
        delay(5);
    }
}


