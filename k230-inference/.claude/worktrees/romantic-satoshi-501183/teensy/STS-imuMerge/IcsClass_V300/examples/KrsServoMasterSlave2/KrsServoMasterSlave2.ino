//
//  @file KrsServoMasterSlave2.ino
//  @brief KRS Servo Master Slave sample program2
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  ID:1のサーボモータの角度を読み取りID:0のサーボモータに読み取った角度を渡します
//  (ID1の動きをID0が真似をします)
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

  int id1Pos;

  //ICS3.5以前の場合
  id1Pos = krs.setFree(1);  //ID1のデータをFreeで読み取ります
  krs.setPos(0,id1Pos); //ID0にID1で受け取ったポジションデータを書き込みます

  //ICS3.6以降の場合
  //id1Pos = krs.getPos(1);  //ID1のデータを角度取得コマンドで受け取ります(ICS3.6のみ)
  //krs.setPos(0,id1Pos); //ID0にID1で受け取ったポジションデータを書き込みます

  Serial.print("ID1 Pos: ");  //ID1から読み出した現在位置を表示
  Serial.println(id1Pos);
}
