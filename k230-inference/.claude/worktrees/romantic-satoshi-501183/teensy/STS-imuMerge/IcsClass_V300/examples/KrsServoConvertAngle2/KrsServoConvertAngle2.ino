//
//  @file KrsServoConvertAngle2.ino
//  @brief KrsServoConvertAngle sample program2
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  Pos,Deg変換でサーボ動作 
//  整数型で変換したものは90度
//  浮動小数点型で変換したものは-90.0度 に動きます。
//  ICSの通信にはHardwareSerialを使います。
//
//  Arduino R4などUSBとシリアルポートが別になっているボードに対応します。
//  Arduinoが受け取ったデータをシリアルモニタに表示できるようになりました。
//

#include <IcsHardSerialClass.h>

const byte EN_PIN = 2;        //Arduinoに接続したenableピンのピン番号
const long BAUDRATE = 115200; //サーボの通信速度
const int TIMEOUT = 1000;     //サーボとのシリアル通信に設定する応答待ち時間

//インスタンス＋ENピン(2番ピン)およびUARTの指定
//ボードに接続しているシリアルポートに合わせて&Serialを変更してください。
//(&Serial / &Serial1 / &Serial2...)
IcsHardSerialClass krs(&Serial1,EN_PIN,BAUDRATE,TIMEOUT);

void setup() {

  Serial.begin(9600); // PCと通信を開始

  krs.begin();        //サーボモータの通信初期設定

}

void loop() {

  //整数型(x100)の場合
  int angle_i = 9000;   //90度を指定
  int pos_i = krs.degPos100(angle_i);  //90 x100deg をポジションデータに変換
  Serial.print("i_変換前: ");   //変換前後のデータを表示
  Serial.println(angle_i);
  Serial.print("i_変換後: ");
  Serial.println(pos_i);

  krs.setPos(0,pos_i);          //変換したデータをID:0に送る
  delay(1000);                  //少し待つ


  //浮動小数点の場合
  float angle_f = -90.0;
  int pos_f = krs.degPos(angle_f);  //-90.0deg(float)をポジションデータに変換
  Serial.print("f_変換前: ");       //変換前後のデータを表示
  Serial.println(angle_f);
  Serial.print("f_変換後: ");
  Serial.println(pos_f);

  krs.setPos(0,pos_f);          //変換したデータをID:0に送る
  delay(1000);                  //少し待つ
}
