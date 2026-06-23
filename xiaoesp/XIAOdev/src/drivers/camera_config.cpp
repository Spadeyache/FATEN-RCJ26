#include "camera_config.h"
#include "esp_camera.h"
#include <Arduino.h>

bool Camera_Init() {
    camera_config_t config;

    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_d7    = Y9_GPIO_NUM;
    config.pin_d6    = Y8_GPIO_NUM;
    config.pin_d5    = Y7_GPIO_NUM;
    config.pin_d4    = Y6_GPIO_NUM;
    config.pin_d3    = Y5_GPIO_NUM;
    config.pin_d2    = Y4_GPIO_NUM;
    config.pin_d1    = Y3_GPIO_NUM;
    config.pin_d0    = Y2_GPIO_NUM;

    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;

    config.xclk_freq_hz = 20000000;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    config.pixel_format = PIXFORMAT_RGB565;
    // config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size   = FRAMESIZE_QQVGA;  // 160x120
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY; // grab lasted slowers the speed as every time it checks to grab

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_reg(s, 0xff, 0xff, 0x01); // Switch to Bank 1 to access CLKRC
        s->set_reg(s, 0x11, 0xff, 0x00); // Set CLKRC to 0 (Fastest internal clock)
                
        // --- Manual exposure ---
        s->set_exposure_ctrl(s, 0);    // Disable AEC (0 = Manual)
        s->set_aec_value(s, 30);      // 30  Manual exposure: 0 to 1200 (Higher = brighter/slower shutter)
        s->set_aec2(s, 0);             // Disable night-mode AEC when in manual

        // --- Manual gain ---
        s->set_gain_ctrl(s, 0);        // Disable AGC (0 = Manual)
        s->set_agc_gain(s, 0); //10       // Manual gain: 0 to 30 (Higher = brighter but more noise)

        // --- Manual white balance ---
        s->set_whitebal(s, 0);         // Disable AWB (0 = Manual)
        s->set_awb_gain(s, 0);         // Disable AWB gain (0 = Manual)
        s->set_wb_mode(s, 0);          // Use 0 for "Custom/Manual" or pick a preset (1: Sunny, 2: Cloudy, etc.)
    

        // // --- Auto exposure ---
        // s->set_exposure_ctrl(s, 1);   // Enable AEC 1 | Sisable AEC 0
        // s->set_aec2(s, 1);            // Enable night-mode AEC 1 | Diable 0
        // s->set_ae_level(s, 0);        // Keep bias neutral to start (no AE bias)

        // // --- Auto gain ---
        // s->set_gain_ctrl(s, 1);       // Enable AGC 1 | Disable 0
        // s->set_gainceiling(s, GAINCEILING_8X);     // GAINCEILING_8X

        // // --- Auto white balance ---
        // s->set_whitebal(s, 1);        // Enable AWB 1 | disable 0
        // s->set_awb_gain(s, 1);        // Enable AWB gain 1 | disable 0




        // --- Image quality ---
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);    // 1 Slight contrast boost to help define line edges
        s->set_saturation(s, 0);  //0 testing 2 for clear color deteciton

        s->set_sharpness(s, 0);
        s->set_denoise(s, 2);         // Light denoise to reduce pixel noise

        // --- Correction ---
        s->set_lenc(s, 1);            // Lens correction ON — fixes vignetting
        s->set_bpc(s, 1);             // Bad pixel correction ON
        s->set_wpc(s, 1);             // White pixel correction ON
        s->set_raw_gma(s, 0);         // Gamma OFF — linear response, better for measurement math

        // --- No effects ---
        s->set_special_effect(s, 0);
        s->set_colorbar(s, 0);

        // --- 180° rotation (hardware, zero per-frame cost) ---
        s->set_vflip(s, 1);   // vertical flip
        s->set_hflip(s, 1);   // horizontal flip


    }
    
    // Warmup: discard first 15 frames so sensor stabilizes
    Serial.println("Camera warming up...");
    for (int i = 0; i < 15; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(50);
    }
    Serial.println("Camera ready.");

    return true;
}

camera_fb_t* Camera_Grab() {
    return esp_camera_fb_get();
}

void Camera_Return(camera_fb_t* fb) {
    if (fb) esp_camera_fb_return(fb);
}


// --- CALIBRATION NOTES ---
// wb_mode = 2 (Cloudy): Fixed color temperature preset chosen to maximize
//   differentiation of red, silver, white, black under white LED lighting.
//   WB gains are then fine-tuned in software via Vision_CalibrateWB().
//
// aec_value = 300: FIXED manual exposure. AEC is disabled.
//   Tune this value (0-1200) for your specific arena lighting so that
//   a white surface averages ~130-150 gray. Higher = brighter.
//   TODO: implement auto-calibration on boot if lighting varies.
//
// agc_gain = 5: FIXED manual gain. AGC is disabled.
//   Set low (5/30) because white LED provides sufficient light.
//   Low gain = less noise = more stable gray averages.
//   Increase only if aec_value maxed out and image still too dark.





