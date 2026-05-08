//
//  @file KrsServoGetParameter.ino
//  @brief KRS ServoKRS Servo parameter get program
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2017/12/26
//
//  ID:0の電流、温度、角度データ(ICS3.6のみ）を取得します。
//  ICSの通信にはHardwareSerialを使います。
//  表示機がないので、実際に動いていないように見えます。
//	あくまでも記述方法を参考にしてください。
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

  Serial.begin(9600); //通信速度を9600bpsに設定

  krs.begin();        //サーボモータの通信初期設定

  krs.setPos(0, 7500);  //中心位置へ移動する
  //krs.setFree(0);     //サーボの角度を手で変更できるようにフリーにする

}

void loop() {

	//温度データの取得
  int rTmp = krs.getTmp(0);     //ID:0の温度データを取得
  Serial.print("Temp: ");
  Serial.println(rTmp);
  if(rTmp != krs.ICS_FALSE) //受信が成功しているか判定
  {
    //成功した時の処理
    Serial.println("Temp Communication: True");
  }
  else
  {
    //失敗した時の処理
    Serial.println("Temp Communication: False");
  }


	//電流データの取得
  int rCur = krs.getCur(0); //ID:0の電流データを取得
  Serial.print("Current: ");
  Serial.println(rCur);
  if(rCur != krs.ICS_FALSE) //受信が成功しているか判定
  {
    //成功した時の処理
    Serial.println("Current Communication: True");
  }
  else
  {
    //失敗した時の処理
    Serial.println("Current Communication: False");
  }


  //ポジションデータを取得(ICS3.6のみ)
  //ICS3.5以前の場合は-1(ICS_FALSE)が返ってきます
  int rPos = krs.getPos(0); //ID:0のポジションデータを取得(ICS3.6のみ)
  Serial.print("Position: ");
  Serial.println(rPos);
  if(rPos != krs.ICS_FALSE) //受信が成功しているか判定
  {
    //成功した時の処理
    Serial.println("Position Communication: True");
  }
  else
  {
    //失敗した時の処理
    Serial.println("Position Communication: False");
  }

  delay(1000);
}
