//
//  @file KrsServoSetId2.ino
//  @brief KRS ID command set program2
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  IDを1に変更し、再度読み込んで書き込んだIDかどうかチェックします
//  ICSの通信にはHardwareSerialを使います。
//  【ご注意ください】複数台のサーボをつないで使用することはできません。
//                   ボードとサーボを1対1でつないで実行してください。
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

  const byte SET_ID = 1;  //変更するID番号

  //IDの設定
  krs.setID(SET_ID);

  delay(100); //少し待つ

  //IDの取得
  int reId = krs.getID();

  Serial.print("Set ID: ");
  if(reId == SET_ID)
  {
    //成功した場合
    Serial.println("True");
  }
  else
  {
    //失敗した場合
    Serial.println("False");
  }

  for(;;);  //処理終了
}
