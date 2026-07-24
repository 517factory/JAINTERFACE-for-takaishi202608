
// ファームウエアバージョン、シリアルモニターログに表示される
#define FW_VER "1.11b"

/*
-Version履歴（ｖ1.XXまでの履歴は過去バージョン参照）
        - ver1.11b 2026/07/25
                - SIMSTATE コマンド対応 (getSimStateInformation / checkSimStatus)
                - v1.11bバージョン作成

        - ver1.11a 2026/07/24
                - sketch_CocoBox3FW_v254i の通信・共通モジュールの移植
                - シリアルコマンド受信用サブシステム (SerialCommand) の追加
                - DIPスイッチ非搭載基板対応 (ENABLE_DIP_SWITCH マクロ追加)
                - SIMMODE=0 設定時の MODE 1 (FIXモード) への自動フォールバックとログ出力対応
                - decodeConfigSetting / LTE_SET コマンド解析および SIMMODE 変更時のモデム再設定シーケンスの対応
                - モデムコマンド応答失敗・プロンプトタイムアウト時のESCキャンセルおよび切断ステート(DISCONNECTED)更新処理の追加
                - 切断検知時のネットワーク再接続リトライおよびリトライ超過時の自動再起動(cbx_restart)機能の追加 (COCOBOX FWと同等化)
                - 切断・復帰時の LedController() 呼び出しおよびコールバック(modemMqttStateCallback)連携修正 (切断時のオレンジLED高速点滅対応)
                - モデム接続成功時の mqttConnectType=CONNECTED フラグ更新を追加 (未送信メッセージのスタック・無限ループ防止)
                - NVSへの BootReason 保存および起動時の BOOT REASON ログ記録・サーバー送信機能の追加 (COCOBOX FWと同等化)

        - ver1.10a 2026/04/14
                - MQTT化

        - ver0.00a 2025/04/04 TEST
                - 基本構成
                        - 基本的にサブモジュールはいじらない
                        - 変更したサブモジュールは頭に”JAI_”を付ける
                - GPIO書き換え（JAI_gpio.h）
                - 不要モジュールの削除
                        - DHT22関連
                        - EQ関連
                        - RFIDモジュール
                        - サーボ制御

        - ver0.01a 2025/04/13
                - cocobox v2.31jをベースに改造
                - 通信までの基本構成終わり

        - ver0.01b 2025/04/18
                TODO
                - LED動作関連
                        -赤は常時点灯
                        -黄色は通信時点灯（COCOBOXと同じ）
                        -緑はJAI発動中点滅。モード解除で消灯
                - PB01の挙動
                        - ONで発砲
                        - JAI発動モード中は発砲しない
                        - JAI発動モード中は長押しでLED消灯、JAI発動モード解除
                - PB02の挙動
                        - 用途未定のため、JA01-08と同じ挙動とする
                - 起動完了までJAXXとPBXXの送信をマスクする

        - ver0.01c 2025/05/04
                -
JAの入力をQUEで制御（連続入力時に送信がうまくいかなくなる対策）

        - ver1.00a 2025/05/06
                - 出荷バージョン（0.01cと同じ）

        - ver1.00b 2025/05/09
                - 電文形式を変更（JAON,JA=10000000,ID=6AE4CC,S10:52:03)
                - サーバーからのRESET指示電文追加(JRST)
                - 緑ランプを高速点滅に変更

        - ver1.00c 2025/05/13
                -
電文形式を変更。サーバー側にてTMP,HUM,VBATがないとエラーになるためダミー追加
                POLL,BGNは必須だが同じコードを通っているのでJAONにも付加。
                JAON,JA=10000000,TMP=0.0,HMY=0.0,VBT=0.00,ID=6AE4CC,S10:52:03

*/