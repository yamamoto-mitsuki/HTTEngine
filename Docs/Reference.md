# HTTEngine リファレンス（索引）

**ゲームを作るときに使う API と書き方。** 章ごとに `Reference/` の下のファイルに分けてある。
探している物を下の表で見つけて、**その章のファイルだけを開く**（全部の章を読まない）。
設計の理由は [ARCHITECTURE.md](ARCHITECTURE.md)。各章は「できること → 良い例 → だめな例」の順。

## 探している物から
| 探している物 | 行き先 |
|---|---|
| ゲームを起動する形・**ファイルをどこに置くか**・名前の決まり（Sound と Audio・Calc と Get） | [01 全体の形](Reference/01-Overview.md) |
| シーンの保存・Play / Stop で何が戻るか・**シーンを切り替える（`LoadScene`）**・幕を付けて切り替える | [02 ゲームの入口](Reference/02-GameEntry.md) |
| Entity を作る・消す・**同じ Entity を何個でも作る（Prefab）** | [03 Entity](Reference/03-Entity.md) |
| Component を付ける・取る | [04 Component](Reference/04-Component.md) |
| **自分の Component を書く（`COMPONENT` / `SYSTEM`）**・`ui.Field` で書ける物・**マウスを乗せると出る説明（項目・enum の選択肢・Component・カテゴリ）** | [05 ゲーム固有 Component](Reference/05-GameComponent.md) |
| **敵が複数いるときのフラグの持ち方**（状態を Component・Script・System のどこに置くか） | [06 状態をどこに置くか](Reference/06-StatePlacement.md) |
| 位置・回転（クォータニオンと角度）・親子（**見た目を変えずに親を付け替える**） | [07 Transform](Reference/07-Transform.md) |
| モデルを出す・マテリアルの上書き・**モデル無しで球・箱を出す**・足場の形・**床・壁に塗る（Decal）**・霧・被写界深度・ブルーム・トーンマップ・色の調整・図形とスプライト | [08 描画](Reference/08-Rendering.md) |
| ライト | [09 ライト](Reference/09-Light.md) |
| **カメラを置く・切り替える・追いかける・回り込む（ロックオン）・揺らす** | [10 カメラ](Reference/10-Camera.md) |
| キー・マウス・パッド（**Game ビューのマウス位置・移動の入力**） | [11 入力](Reference/11-Input.md) |
| 音（AudioSource・BGM / SE の音量・3D の音・SoundManager） | [12 音](Reference/12-Sound.md) |
| 経過時間・ポーズ（**スロー・ヒットストップ**） | [13 時間](Reference/13-Time.md) |
| 乱数・数学・**当たったかどうか（Collider）**・OnCollision / OnTrigger・**Raycast・SweepSphere**・弾む床・風・範囲に入ったら出す・Alpha Group | [14 数学と当たり判定](Reference/14-MathCollision.md) |
| **パーティクル（Particle System）**・**軌跡（Trail）** | [15 パーティクル・軌跡](Reference/15-ParticleTrail.md) |
| ログ・アサート・調整項目（GlobalVariables） | [16 ログ・アサート・調整項目](Reference/16-LogAssertParams.md) |
| エディタの使い方（Hierarchy・画面の編集） | [17 エディタ](Reference/17-Editor.md) |
| やりがちな失敗の一覧 | [18 だめな例まとめ](Reference/18-Pitfalls.md) |
| まだ無い物 | [19 まだ無い物](Reference/19-NotYet.md) |
| UI の図形（Vector Shape）・線（Polyline）・切り抜き（UI Mask）・模様・文字（Text）・画面の比率と黒帯・**UI をワールドの物に重ねる（UI World Follow）**・**ポーズ画面・クリア画面（UI Screen）**・UI State | [20 UI](Reference/20-UI.md) |
| 時間で値を変えるカーブ（Curve）・時間に沿って動かす（Timeline） | [21 カーブと Timeline](Reference/21-CurveTimeline.md) |
| **Script を書く（`SCRIPT`・Awake / Start / Update・`Fields`・GameObject）**・COMPONENT / SYSTEM との使い分け | [22 Script](Reference/22-Script.md) |
| **アニメーションを流す（Animator・名前で選ぶ・`AnimationSystem::Play`）** | [23 アニメーション](Reference/23-Animation.md) |
| **キャラを歩かせる（Character Controller）**・坂・段・ジャンプ・地面にいるか | [24 Character Controller](Reference/24-CharacterController.md) |
| **攻撃の当たり（Hitbox・Hurtbox）・HP とダメージ（Health）**・ジャスト判定・床の予告・**ボスの攻撃表の AI（Attack Table・攻撃部品）** | [25 攻撃の当たりとダメージ](Reference/25-HitDamage.md) |

## 直すとき
- 1つの章が大きくなりすぎたら（目安 250 行）、章を分けてこの表に行を足す。
- 章をまたぐリンクは `NN-Name.md#見出しのアンカー` で書く。
- 消した md・ゲームの名前（特定のゲームのクラス名など）を書かない。
