#include <lvgl.h>
#include <TFT_eSPI.h>
#include <FT6236.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUDP.h>
#include <NTPClient.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

// 全局变量
bool use_24h_format = true;  // 默认使用24小时制
bool show_date = true;       // 默认显示日期
time_t last_sync_time = 0;   // 上次同步时间

// 函数声明
void update_temp_humi(lv_timer_t* t);

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

// 触摸屏读取回调函数，用于处理触摸事件
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    if (!ts.touched())
    {
        data->state = LV_INDEV_STATE_REL;  // 未触摸状态
        isTouching = false;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;  // 触摸状态
        isTouching = true;

        TS_Point p = ts.getPoint();
       /* // 根据屏幕旋转调整坐标（假设屏幕旋转0度）
        //data->point.x = screenWidth - p.y;  // 交换X/Y轴并镜像
        //data->point.y = p.x;
                et the coordinates*/
        data->point.x = p.x;
        data->point.y = p.y;

        // 打印触摸信息
        Serial.printf("Touch detected at X: %d, Y: %d\n", data->point.x, data->point.y);
    }
    //按键1的检测区域是 X: 120, Y: 300//
    //按键2的检测区域是 X: 240, Y: 300//
    //按键3的检测区域是 X: 400, Y: 300//
if (data->state == LV_INDEV_STATE_PR && 
        data->point.y == 300)
    {
        if (data->point.x == 120 )
        {
            // 触摸按键1区域，切换12/24小时制
            use_24h_format = !use_24h_format;
            Serial.printf("Time format changed to %s\n", use_24h_format ? "24h" : "12h");
            lv_timer_reset(update_timer);  // 立即更新时间显示
        }
        else if (data->point.x == 240 )
        {
            // 触摸按键2区域，切换日期显示
            show_date = !show_date;
            Serial.printf("Date display %s\n", show_date ? "enabled" : "disabled");
            lv_timer_reset(update_timer);  // 立即更新时间显示
        }
        else if (data->point.x == 400 )
        {
            // 触摸按键3区域，执行其他操作
            Serial.println("Button 3 pressed");
        }
    }
}



// 创建时间标签，并添加触摸事件处理
lv_obj_t* create_time_label()
{
    // 设置深色主题
    lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                         LV_THEME_DEFAULT_DARK, &lv_font_montserrat_16);

    // 创建日期标签
    lv_obj_t* date_label = lv_label_create(lv_scr_act());
    lv_label_set_text(date_label, "2025-03-24");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(date_label, LV_OPA_TRANSP, 0);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 20);

    // 创建时间标签
    lv_obj_t* label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "00:00:00");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    
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
    
    // 添加触摸事件
    lv_obj_add_event_cb(label, [](lv_event_t * e) {
        static uint32_t last_tap = 0;
        uint32_t now = millis();
        
        // 防止双击误触发
        if (now - last_tap < 500) return;
        last_tap = now;
        
        // 切换12/24小时制
        use_24h_format = !use_24h_format;
        Serial.printf("Time format changed to %s\n", use_24h_format ? "24h" : "12h");
        
        // 长按切换日期显示
        if (lv_event_get_param(e) && 
            lv_indev_get_scroll_obj(lv_event_get_indev(e))) {
            show_date = !show_date;
            Serial.printf("Date display %s\n", show_date ? "enabled" : "disabled");
        }
        
        // 立即更新时间显示
        lv_timer_reset(update_timer);
        Serial.println("Time display updated");
    }, LV_EVENT_CLICKED, NULL);
    
    Serial.println("Time label created with touch support");  // 添加调试信息
    time_label = label;

    // 创建AM/PM标签
    ampm_label = lv_label_create(lv_scr_act());
    lv_label_set_text(ampm_label, "");
    lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_16, 0);  // 设置AM/PM字体
    lv_obj_set_style_text_color(ampm_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ampm_label, LV_OPA_TRANSP, 0);
    lv_obj_align_to(ampm_label, time_label, LV_ALIGN_OUT_RIGHT_MID, -20, 20);  // 对齐到时间标签的右侧

    return date_label;
}

// 创建状态栏，显示WiFi状态和IP地址
lv_obj_t* create_status_bar()
{
    // 创建状态栏对象
    lv_obj_t* status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar, screenWidth, 30);  // 设置状态栏的宽度和高度
    
    // 设置状态栏样式
    static lv_style_t status_style;
    lv_style_init(&status_style);
    lv_style_set_bg_opa(&status_style, LV_OPA_COVER);  // 设置背景透明度为完全不透明
    lv_style_set_bg_color(&status_style, lv_palette_darken(LV_PALETTE_BLUE, 3));  // 设置背景颜色
    lv_style_set_shadow_width(&status_style, 10);  // 设置阴影宽度
    lv_style_set_shadow_ofs_x(&status_style, 0);  // 设置阴影水平偏移
    lv_style_set_shadow_ofs_y(&status_style, 2);  // 设置阴影垂直偏移
    lv_style_set_shadow_color(&status_style, lv_palette_darken(LV_PALETTE_BLUE, 4));  // 设置阴影颜色
    lv_obj_add_style(status_bar, &status_style, 0);  // 应用样式到状态栏
    
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, 0);  // 将状态栏对齐到底部中间位置

    // 创建WiFi状态标签
    wifi_label = lv_label_create(status_bar);
    lv_label_set_text(wifi_label, "");  // 初始化文本为空
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);  // 设置文本颜色为白色
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, -10, 0);  // 将WiFi状态标签对齐到状态栏左侧中间位置，稍微向左偏移

    // 创建IP地址标签
    ip_label = lv_label_create(status_bar);
    lv_label_set_text(ip_label, "");  // 初始化文本为空
    lv_obj_set_style_text_color(ip_label, lv_color_white(), 0);  // 设置文本颜色为白色
    lv_obj_align(ip_label, LV_ALIGN_RIGHT_MID, 0, 0);  // 将IP地址标签对齐到状态栏右侧中间位置

    return status_bar;
}

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
    float temp = sht31.readTemperature();
    float humi = sht31.readHumidity();

    if (!isnan(temp) && !isnan(humi)) {
        char tempStr[20];
        char humiStr[20];
        snprintf(tempStr, sizeof(tempStr), "Temp: %.1f°C", temp);
        snprintf(humiStr, sizeof(humiStr), "Humi: %.1f%%", humi);
        lv_label_set_text(temp_label, tempStr);
        lv_label_set_text(humi_label, humiStr);
    }
}
// 初始化函数
void setup()
{
    Serial.begin(115200);

    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 20);
    Serial.printf("Display buffer initialized, size: %d\n", screenWidth * 20);  // 添加调试信息

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    if (!ts.begin(40,3,2)) { // 确认I2C引脚配置正确
        Serial.println("Touchscreen init failed!");
    }

    // WiFi连接
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    if (!wm.autoConnect("SmartClockAP")) {
        Serial.println("Failed to connect");
        ESP.restart();
    }

    // 时间服务
    timeClient.begin();
    timeClient.forceUpdate();

    // 初始化SHT30
    Wire.begin();
    bool sht31_found = sht31.begin(0x45);
    if (!sht31_found) {
        Serial.println("Couldn't find SHT30");
    }

    // 创建界面
    date_label = create_time_label();
    create_status_bar();
    update_wifi_info();

    // 根据SHT30是否找到决定是否创建温湿度标签
    if (sht31_found) {
        temp_label = lv_label_create(lv_scr_act());
        lv_label_set_text(temp_label, "Temp: --.-°C");
        lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
        lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 20, 120);

        humi_label = lv_label_create(lv_scr_act());
        lv_label_set_text(humi_label, "Humi: --.-%");
        lv_obj_set_style_text_font(humi_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(humi_label, lv_color_white(), 0);
        lv_obj_align_to(humi_label, temp_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    }

    // 创建定时器（每秒更新）


    // 更新温湿度
    if (sht31_found) {
        lv_timer_create(update_temp_humi, 5000, NULL);
    }

    // 每30秒检查WiFi连接
    update_timer = lv_timer_create(update_time, 1000, NULL);
    lv_timer_set_repeat_count(update_timer, LV_ANIM_REPEAT_INFINITE);
    lv_timer_ready(update_timer);

    // 内存监控
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("Free memory: %d bytes (%.1f%%)\n",
        mon.free_size,
        (float)mon.free_size / mon.total_size * 100);
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