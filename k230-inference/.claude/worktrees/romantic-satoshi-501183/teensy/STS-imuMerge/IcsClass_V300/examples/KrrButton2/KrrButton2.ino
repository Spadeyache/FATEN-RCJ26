//
//  @file KrrButton2.ino
//  @brief KRR get button sample2 
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  KRR-5(受信機)に送られてきているボタンデータを取得します
//  左側で押されたボタンをもとにID:0のサーボモータを動かします。
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

  krs.begin();  //サーボモータの通信初期設定
  
}

void loop() {

  unsigned short buttonData;

  buttonData = krs.getKrrButton();    //KRS-Sのボタンデータを読み取る
  Serial.print("Button Data: ");      //buttonDataをシリアルモニタに表示
  Serial.println(buttonData);

  Serial.print("Communication: ");    //受信機との通信状態をシリアルモニタに表示
  if(buttonData != KRR_BUTTON_FALSE)  //受信が成功しているか判定
  {
    //受信が成功している場合の処理
    Serial.println("True");

    switch(buttonData)        //ボタンデータによって処理を分岐
    {
      case KRR_BUTTON_UP :    //↑ボタンだった時
      {
        krs.setPos(0,7500);   //7500(中央)へ移動
        break;
      }
      case KRR_BUTTON_DOWN :  //↓ボタンだった時
      {
        krs.setFree(0);       //サーボをフリーにする
        break;
      }
      case KRR_BUTTON_RIGHT:  //→ボタンだった時
      {
        krs.setPos(0,krs.degPos100(9000));  //90度へ移動
        break;
      }
      case KRR_BUTTON_LEFT:   //←ボタンだった時
      {
        krs.setPos(0,krs.degPos100(-9000));     //-90度へ移動
        break;
      }
      case (KRR_BUTTON_UP | KRR_BUTTON_RIGHT):  //↑→ボタンだった時
      {
        krs.setPos(0,krs.degPos100(4500));      //45度へ移動
        break;
      }
      case (KRR_BUTTON_UP | KRR_BUTTON_LEFT):   //←↑ボタンだった時
      {
        krs.setPos(0,krs.degPos100(-4500));     //-45度へ移動
        break;
      }
      case (KRR_BUTTON_DOWN | KRR_BUTTON_RIGHT): //↓→ボタンだった時
      {
        krs.setPos(0,11500);                    //270度(最右端)へ移動
        break;
      }
      case (KRR_BUTTON_DOWN | KRR_BUTTON_LEFT): //←↓ボタンだった時
      {
        krs.setPos(0,3500);                     //-270度(最左端)へ移動
        break;
      }
      default:
      {
        //なにもしない
        break;
      }
    }   
  }
  else
  {
    //受信に失敗した場合の処理
    Serial.println("False");
  }

  delay(10);    //KRR-5は10ms以下の応答にはついていけないので待つ
}
