//
//  @file KrsServoSetParameter2.ino
//  @brief KRS Servo Parameter set program2
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  ID:0のストレッチ、スピードを設定します
//  ストレッチもスピードも90に設定し続けるので、実際は動いていないように見えます
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

  krs.setPos(0, 7500);  //サーボをトルクONにして保持する
                        //ストレッチを変更した時の確認用

}

void loop() {

  int flag;

  //ストレッチを設定
  int strc = 90;
  flag = krs.setStrc(0, strc);   //ID0のストレッチを90にします
  Serial.print("Stretch: ");  //成功すると書き込んだデータがそのまま返ってきます
  Serial.println(flag);
  if(strc == flag) //受信が成功しているか判定
  {
    //成功した時の処理
    Serial.println("Stretch Set: True");
  }
  else
  {
    //失敗した時の処理
    Serial.println("Stretch Set: False");
  }

  int spd = 90;
  flag = krs.setSpd(0, spd);  //ID0のスピードを90にします
  Serial.print("Speed: ");  //成功すると書き込んだデータがそのまま返ってきます
  Serial.println(flag);
  if(spd == flag) //受信が成功しているか判定
  {
    //成功した時の処理
    Serial.println("Speed Set: True");
  }
  else
  {
    //失敗した時の処理
    Serial.println("Speed Set: False");
  }

  delay(1000);
}
