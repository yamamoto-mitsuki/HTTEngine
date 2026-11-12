> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 17. エディタ

| 操作 | 何ができるか |
|---|---|
| **Hierarchy** | Entityの一覧。右上の＋で作る（Create Entity・Create Child・**Folder**・Light・**3D Object → Polygon・Arrow Strip・Wind Zone・Trigger Activator**・**Effects → Particle System**・**Camera**・Sprite・Image・Vector Shape・Polyline・Text）、右クリックで Rename・Copy（Ctrl+C）・Paste（Ctrl+V）・Paste As Child・Duplicate（Ctrl+D）・Save As Prefab・Create Child・Delete（Del）、ダブルクリック（F2）で名前変更、ドラッグで親子（**見た目の位置は変わらない**） |
| **Hierarchy の目** | 行の左の目を押すと、その Entity と子を **Scene ビューだけで隠す**（Game ビュー・ゲームの動きは変わらない。クリックでも選ばれない。保存しない）。斜線の目が隠している物、薄い斜線は親を隠したので隠れている物。描く物は1回だけ積んで、Scene ビューで `RenderQueue` が飛ばす（Config の `isSceneHidden`） |
| **Hierarchy の並び** | 作った順（Entity を消しても、ほかの並びは変わらない）。並び替える機能は無い |
| **画面の編集（Edit UI Screen）** | `.uiscreen` をダブルクリック。Hierarchy は画面の中身だけ（一番上に Done・Cancel）、Scene ビューに水色の枠。後ろのシーンは見えるが選べない。中身は「シーンに保存しない」印（`Entity::isDontSave`）付き |
| **Hierarchy のフォルダ** | ＋ → **Folder**。整理するための入れ物（印の `FolderComponent` を付けた Entity）。中へはドラッグで入れる。Transform は原点のまま使う（Inspector に出さない・ギズモも出ない）。フォルダを入れられるのは一番上か、ほかのフォルダの中だけ。無効にすると中身ごと無効。フォルダの右クリック → Create Folder で中にフォルダ |
| **Inspector** | 選んだEntityのComponentを編集。下の **+ Add Component** で足す、見出しの右クリックで外す。マウスを乗せると説明が出る（見出し：何をする Component か。項目：その値の意味。enum の欄は開いて選択肢に乗せると、その選択肢の意味）。Add Component ではカテゴリ・Component に乗せると説明が出て、右の列の上に選んだカテゴリの説明が出る |
| **Transform の XYZ ボタン** | Position・Rotation・Scale の左。押すと X・Y・Z を一緒に変える（Position・Rotation は同じだけ足す、Scale は同じ倍＝形を保つ。0 からは同じだけ足す）。押している間は黄色。保存しない（エディタを閉じると外れる） |
| **Scene / Game** | Sceneは編集用（デバッグカメラ・ギズモ）、Gameは製品の絵。Polygon Shape の **Edit Shape**・Arrow Strip の **Edit Path**・Polyline の **Edit Points** の間は、Scene ビューで点を動かす（移動のギズモは出ない） |
| **Scene ビューの UI** | Image・Vector Shape・Polyline・Text をクリックで選び、**ドラッグで動かす**（1 ピクセルずつ。1回のドラッグで1件の履歴）。Vector Shape は枠の白い印をドラッグすると **Size が変わる**（反対の辺は動かない）。UI は 3D の物より先に選ぶ（重なっていたら手前に描いてある物）。UI には3D の移動のギズモは出ない。UI の後ろの物を選ぶときは **View → Scene View → Pick UI** を外す |
| **Play / Pause / Stop** | Playで `Update` が回り出す（**停止中に編集した状態のまま始まる**）。**Stopで Play を押した瞬間の状態へ戻る**（Play 中に動いた・作った・消した物は全部戻る。選んでいた Entity もそのまま） |
| **Restart** | 再生中だけ押せる。Play を押した瞬間からやり直す |
| **Save（Ctrl+S）** | Control ウィンドウ。停止中だけ。シーンファイル（`resources/scenes/〇〇.scene.json`）に書く。1つ前は `.bak` に残る |
| **カーブ（`.curve`）** | Project で選ぶと Inspector に出る。クリックで選ぶ・つまんで動かす、ダブルクリックでキーを足す（線の近くなら形を変えない）、Delete で消す、ホイールで拡大縮小（Ctrl：時間だけ、Shift：値だけ）、中ボタンか Alt+左ボタンで移動、F で全体。Ease Preset はキーを選んでいれば次のキーまでの区間に、選んでいなければ全体に入る。直すと自動で保存（1つ前は `.bak`） |
| **Timeline（`.timeline`）** | Project で Create → Timeline。Timeline ウィンドウで直す（ダブルクリック・Inspector の Open Timeline・Timeline の付いた Entity を選ぶ）。トラック・クリップ・キー・カーブ・停止中のプレビュー。直すと自動で保存（1つ前は `.bak`）。Ctrl+Z で戻せる |
| **Undo / Redo** | Ctrl+Z / Ctrl+Y（Ctrl+Shift+Z でも Redo）。作成・削除・名前・有効・親子・Componentの追加削除・Inspectorの値変更・**Settings（PostEffect など）の値変更**・**カーブの編集**が、やった順に戻る。Play・Stop・シーン切り替えで履歴は消える |
| **Project** | `resources/` のファイル。左にフォルダの木、右に中身（画像は小さい絵）。ダブルクリックで開く（シーン・`.uiscreen`・`.timeline`・コード）。右クリックで Create（Folder・Scene・Physic Material・Attack Table・Curve・Material・Timeline・UI Screen）・Rename（F2。ほかのシーン・Prefab などに書いてあるパスも書き換える。コードは書き換えず警告だけ）・Delete（ごみ箱へ）・Copy Path・Show in Explorer、Prefab は Place in Scene（原点に置く）。`.physmat`・`.material`・`.curve`・`.attacktable` などは選ぶと Inspector で直せる。右上の数字でアイコンの大きさ |
| **File** | New Scene（Ctrl+N）・Open Scene・Save（Ctrl+S）・Save As（Ctrl+Shift+S）。停止中だけ |
| **Settings（Edit → Settings）** | 調整項目（前の Parameters ウィンドウ）。Project：Audio（音量）・PostEffect・Physics（World → Gravity）・Layers・DamageTypes・ゲームのグループ／Scene：開いているシーンの名前だけ（まだ項目は無い）／Editor：Scene View（Scene ビューのカメラ）。ページごとに Save / Revert、保存していなければ `*`（[16章](16-LogAssertParams.md)） |
| **Keymap（Settings → Editor → Keymap）** | エディタの操作のキー・ボタンを人ごとに変える。割り当てのボタンを押してから、新しいキー・ボタンを押すと入れ替わる（Ctrl・Shift・Alt を押しながら押すと組になる。Esc でやめる）。＋で足す・×で外す・「既定」で戻す。ほかとぶつかると赤くなり、相手の名前と「相手の方を外す」が出る。上のボタンでカメラの操作を **Unity 風・Blender 風** にできる。Save で `Parameters/Editor/Keymap.json`（git に入らないので人ごと）。この章に書いたキーは、全部既定の割り当て |
| **Scene ビューのカメラ** | 既定は、ホイールボタンを押しながらドラッグで向きを変える（その場で回る）・右ドラッグで平行移動・ホイールで前後。Keymap で変えられる（前後に動くドラッグも足せる） |
| **View → Gizmos** | Light Icons・Light Ranges（ライトのアイコンと範囲）・Camera Icons・Camera Paths（カメラのアイコンと線）・Camera Orbits（Camera Orbit の回る輪・回る中心）・Particle Shapes（粒の出る形）は Scene ビューだけに出る。Camera Follow Zones（Camera Follow の Dead Zone・Soft Zone・Bounds。Zone Space が Screen の範囲だけは Game ビューだけ）・Colliders（Collider の線。Character Controller・Wind Zone の線も一緒）・Hit Shapes（攻撃の形・ジャスト判定の範囲・Hurtbox）・Skeletons（骨）は Game ビューにも出る（Release では出ない）。Camera Follow と Particle System は、1つずつ Inspector の Show Gizmo でも消せる |
| **View → Scene View → Post Effects** | Scene ビューにもポストエフェクト（ブルームなど）を掛けるか（既定 ON）。Lens は Scene ビューでは掛けない（ギズモ・クリックの位置がずれるので） |
| **View → Scene View → Fog** | Scene ビューにも霧（Fog）を掛けるか（既定 ON）。Game ビューにはいつも掛ける |
| **View → Scene View → Depth of Field** | Scene ビューにも被写界深度を掛けるか（既定 ON。Post Effects を切っていると押せない）。Game ビューにはいつも掛ける |
| **View → Scene View → Grid・Game View → Grid** | グリッドを出すか。Scene は既定 ON、Game は既定 OFF（遊ぶときと同じ見た目にするため）。描く物は1回だけ積んで、ビューごとに `RenderQueue` が飛ばす（`DrawLayer::EditorGrid`） |
| **Control の Scene:** | 開いているシーンの名前と、保存するファイル。保存していなければ名前に `*`。再生中に `LoadScene` で進んだら「→ 今は Result」 |
| **Profiler → GPU → Stats** | Draw Calls・Skin Buffers（CS で曲げるスキンのあるモデルのバッファの数）・Frame Upload・**SRV Slots**（テクスチャの棚の使っている数。256個〈0番は ImGui〉しかないので、Play / Stop を繰り返しても増え続けないことをここで見る） |

> **Save しないで終了すると、停止中の編集は消える**（Play → Stop では消えない）。Play 中の変更は Stop で戻るので、Play 中に見つけた良い値は、Stop してから入れ直して Save する（Unity と同じ）。
