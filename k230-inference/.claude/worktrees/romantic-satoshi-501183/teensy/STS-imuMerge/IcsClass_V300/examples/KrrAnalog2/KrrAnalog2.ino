//
//  @file KrrAnalog2.ino
//  @brief KRR get Analog sample2 
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  KRR-5(受信機)に送られてきているアナログデータを取得します。
//  ICSの通信にはHardwareSerialを使います。
//  アナログデータをもとにID:0のサーボモータを動かします。
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

  int analogData;
  int sendpos;

  analogData = krs.getKrrAnalog(1);   //KRS-5のPA1に接続されているアナログデータを読み取る
  Serial.print("Analog Data: ");      //analogDataをシリアルモニタに表示
  Serial.println(analogData);

  Serial.print("Communication: ");    //受信機との通信状態をシリアルモニタに表示
  if(analogData != krs.ICS_FALSE)  		//受信が成功しているか判定
  {
    //受信が成功している場合の処理
    Serial.println("True");

    //受信データをサーボのポジションに変換
    sendpos = 7500 + ((analogData - 64) * 62);
    //  受信データ - 64(7bit/2) <= 中央値からどれくらい離れているか計算
    //  *62                    <= 4000(ポジションは±4000)/64 = 62.5　だが、切り捨てて62
    //  7500 + ( )             <= サーボモータの0点からの角度に変換

    krs.setPos(0,sendpos);            //サーボ(ID:0)に計算した値を送る
  }
  else
  {
    //受信に失敗した場合の処理
    Serial.println("False");
  }
  
  delay(10);    //KRR-5は10ms以下の応答を処理できないため待つ
}
