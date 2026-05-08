//
//  @file KrsServo2.ino
//  @brief KrsServoSample2
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  ID:0のサーボをポジション指定で動かす
//  範囲は、左5500 - 中央7500 - 右9500
//  0.5秒ごとに指定数値まで動く
//  ICSの通信にはHardwareSerialを使います。
//

#include <IcsHardSerialClass.h>

const byte EN_PIN = 5;        //Arduinoに接続したenableピンのピン番号
const long BAUDRATE = 115200; //サーボの通信速度
const int TIMEOUT = 1000;     //サーボとのシリアル通信に設定する応答待ち時間

//インスタンス＋ENピン(2番ピン)およびUARTの指定
//ボードに接続しているシリアルポートに合わせて&Serialを変更してください。
//(&Serial / &Serial1 / &Serial2...)
IcsHardSerialClass krs(&Serial1,EN_PIN,BAUDRATE,TIMEOUT);

void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  krs.begin();  //サーボモータの通信初期設定

}

void loop() {

  krs.setPos(0,7500);      //位置指令　ID:0サーボを7500へ 中央
  delay(500);              //0.5秒待つ

  krs.setPos(0,9500);      //位置指令　ID:0サーボを9500へ 右
  delay(500);              //0.5秒待つ

  krs.setPos(0,7500);      //位置指令　ID:0サーボを7500へ 中央
  delay(500);              //0.5秒待つ

  krs.setPos(0,5500);      //位置指令　ID:0サーボを5500へ 左
  delay(500);  
}
