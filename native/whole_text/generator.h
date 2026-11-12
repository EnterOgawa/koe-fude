//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file generator.h
 * @brief 固定rendererと有界な思考生成kernelの内部契約を定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 CPU backendを差替える有限kernelを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の生成しない計数と有限計画の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1419のbackend契約を実装・caller・固定APIへ照合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/whole_text_bounded_result.h"
#include "core/whole_text_plan.h"
#include "inference/whole_text_native_abi.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace koefude::whole_text {
inline constexpr std::uint32_t K_MAXIMUM_BYTES = 8 * 1024 * 1024 ; // 一callのbyte上限
inline constexpr std::uint32_t K_VOCABULARY_TOKENS = 262144 ; // 固定語彙数
inline constexpr char		   K_ENGINE_COMMIT[] = "b1ff4ca23630ac7c5a275405353ddb5c486332c9" ; // engine pin
inline constexpr char		   K_TEMPLATE_SHA256[] =
	"ae53464bf3be25802b3a5b37def7fd89667067d7577049b3b2d74c4d8de4c6d4" ; // template pin

/** @brief 内容を持たない内部失敗。C entryがPODへ変換します */
struct NativeWholeTextError {
	/// 固定status
	KoefudeWtStatus		status ;
	/// 固定分類
	KoefudeWtDiagnostic diagnostic ;
	/// 元decode符号
	std::int32_t		engineCode = 0 ;
} ;
/** @brief 完成Finalと実counterだけを所有します */
struct NativeWholeTextGenerated {
	/// 無編集Final、raw/Analysisを外へ出さない
	std::string	  body ;
	/// 実prefix
	std::uint32_t prefixTokens = 0 ;
	/// opening/自然closing込み
	std::uint32_t analysisSampledTokens = 0 ;
	/// 制御closing成功数
	std::uint32_t closingControlTokens = 0 ;
	/// 自然EOG込み
	std::uint32_t finalSampledTokens = 0 ;
} ;

/** @brief 解放の完遂と元codeを区別します */
struct NativeWholeTextCleanup {
	/// 不明なら再free禁止
	bool		  released = false ;
	/// 元cleanup code
	std::uint32_t code = 0 ;
} ;
/**
 * @brief 一ownerのmodel・借用vocab・CPU context・samplerをkernelから逐次操作する境界です
 * @note 同一ownerの呼出し・破棄はworkerが直列化し、再入・同時操作を行いません。基底にlockはありません
 * modelを開いてmetadataを照合し、語彙だけの計数か、tensor付きmodelでcontext/samplerを開く生成へ進みます
 * contextは同時に一つとし、decode後のlogits検査・sample・次decodeを順に実行します
 * 非noexcept methodはNativeWholeTextErrorや確保時のstd::bad_alloc等を送出し得ます。基底は例外変換しません
 * 途中失敗でも取得済み資源の責任は派生ownerに残ります。callerはcloseを行い、C entryが例外を変換します
 * closeは明示終了後と派生dtorから再度呼ばれ得るため、解放済み資源を再freeしない管理は派生側の責務です
 * V2のchunk解放と解放不明時の保持を使う派生はcloseChunk/retainUnknownを実装します。基底既定は代行しません
 */
class NativeWholeTextBackend {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 派生部分の破棄後に基底部分を破棄します
	 * @note default実装は例外を送出せず、model/context/samplerやbackendを解放しません。解放は派生ownerが担当します
	 * Llama実装の派生dtorはcloseを呼びます。全利用・callbackの終了後に同じworkerで破棄します
	 */
	//------------------------------------------------------------------------------
	virtual ~NativeWholeTextBackend() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief renderer・語彙条件・生成laneを選ぶ不変の方式識別を返します
	 * @return 基底既定は常にGEMMA_THOUGHT_FINALです。Llama実装は固有DLLの構成に応じて
	 * MINISTRAL_PLAIN又はGEMMA_THOUGHT_FINALを返し、成功/失敗やmodelのopen状態は表しません
	 * @note model未openでも取得でき、資源を確保しません。別方式の派生は明示overrideします
	 */
	//------------------------------------------------------------------------------
	virtual WholeTextGenerationKind generationKind() const noexcept
	{
		return WholeTextGenerationKind::GEMMA_THOUGHT_FINAL ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現語彙のtokenが制御tokenかを分類し、本文へ混入する制御IDの拒否に使います
	 * @param[in] token 現modelの語彙内の非負token IDです。IDの範囲検査はcallerが先に行います
	 * @return 基底既定はisEog(token)の真偽だけを返します。Llama実装は語彙の制御属性を返し、
	 * falseは呼出し失敗でなく「制御tokenでない」という分類です
	 * @pre model/vocabが生存していること。EOG以外の制御tokenを区別する方式はoverrideが必要です
	 * @note IDをdecodeせず、語彙・contextを変更しません。基底はisEogの例外を変換しません
	 */
	//------------------------------------------------------------------------------
	virtual bool isControl( std::int32_t token) const
	{
		return isEog( token) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じhost leaseが保護するmodelを一つ読み込み、そのmodel内のvocabを借用します
	 * @param[in] path NULを含まない1～32767 UTF-16 code unitのpathをreturnまで借用します
	 * dataは非NULLで、終端NULはview長へ含めません。現callerはview直後の終端も先に検査します
	 * @param[in] vocabOnly trueはtensorを読み込まない語彙専用、falseはCPU生成用tensorも読み込む指定です
	 * @throws NativeWholeTextError Llama実装では既存model、UTF-16変換失敗、load失敗/取消し、vocab不在です
	 * @pre model/context/samplerが閉じていること。語彙専用からtensor付きへはcloseModelを挟みます
	 * @post 成功時はmodelと借用vocabを保持します。Llama実装はGPUを使わず、backend初期化を初回まで遅延します
	 * @note 同期loadは停止callbackを使います。失敗時も初期化済みbackendや部分modelはcloseの対象です
	 */
	//------------------------------------------------------------------------------
	virtual void				   openModel( std::wstring_view path, bool vocabOnly) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現modelを解放し、そのmodelから借用したvocabを無効にします
	 * @pre 同modelを使うcontext/samplerが閉じ、語彙操作が終了していること
	 * @post Llama実装ではvocabを先にNULL化し、modelがあればfree後にNULL化します。modelなしは何もしません
	 * @note backend初期化状態は保持し、同leaseの再openに使います。基底は解放処理を実装しません
	 */
	//------------------------------------------------------------------------------
	virtual void				   closeModel() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 開いたmodelを固有方式の語彙数・全EOG集合・固定chat templateへ照合します
	 * @throws NativeWholeTextError Llama実装の固定metadata不一致やtemplateのhash計算失敗です
	 * @pre model/vocabのopenが成功していること。語彙専用とtensor付きの各open後に行います
	 * @post 成功してもcontext/samplerは開きません。不一致時もmodelの解放責任はownerに残ります
	 * @note Llama実装はPlain固有のarchitecture/KV geometry/tokenizer/BOSも照合し、modelを修正しません
	 */
	//------------------------------------------------------------------------------
	virtual void				   validateMetadata() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief BOS/EOSを自動追加せず、特殊tokenを解析して入力全byteを一度tokenizeします
	 * @param[in] text callerが検証したUTF-8 byte列をreturnまで借用します。終端不要、長さはINT32_MAX以下、
	 * dataは空でも非NULLとします。空入力は0件になり得ますが、現callerは非空だけを渡します
	 * @param[out] tokens token IDの書込先をreturnまで借用します。要素数が容量でINT32_MAX以下、入力と重ねません
	 * 容量0はNULL data可です。非負戻り値個だけが出力され、残る要素は変更しません
	 * @return Llama実装は上流の符号を保持し、非負なら容量以下のtoken数、容量不足なら必要token数の負値、
	 * 上流の件数上限に達した場合はINT32_MINです。0は空のtoken列で、非空prefixの成功を表しません
	 * @pre live vocabがあり、metadata照合済みであること
	 * @post 上流の容量不足/overflowではbufferは未変更です。例外時のbufferを有効ID列として使いません
	 * @note prefix callerは1～4096件、markerは1件だけ受理します。counterはINT32_MINと容量以下の負必要数を
	 * 拒否し、それ以外の負値は不足件数として保持します。容量拡大・自動再試行・UTF-8検証はここでは行いません
	 */
	//------------------------------------------------------------------------------
	virtual std::int32_t		   tokenize( std::string_view text, std::span<std::int32_t> tokens) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief BOS/EOSを除去せず、特殊tokenも描画する設定でtoken列を一度byte列へ復号します
	 * @param[in] tokens 現語彙内の非負ID列をreturnまで借用します。要素数はINT32_MAX以下、空列はNULL data可です
	 * @param[in] maximumBytes 所有出力bufferの容量上限byteです。0～K_MAXIMUM_BYTESで、終端用byteを数えません
	 * @return 上流の返したbyte数に縮めた独立所有stringです。空列等では空になり、string用終端NULはsize外です
	 * @throws NativeWholeTextError Llama実装では負の必要byte数又は上限超過をOUTPUT_BYTESとして拒否します
	 * @pre live vocabがあり、全IDがその語彙内であること。最大byte数はINT32_MAXにも収まる必要があります
	 * @post 失敗時は部分stringを返さず、model/vocabは保持します。確保失敗の標準例外もcallerへ伝わります
	 * @note 容量を増やして再試行しません。返すbyteのUTF-8完全性、NUL不在、非空本文、完全往復は別のcaller検査です
	 */
	//------------------------------------------------------------------------------
	virtual std::string			   detokenize( std::span<const std::int32_t> tokens, std::uint32_t maximumBytes) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief tensor付きmodelから一つの固定CPU contextを開きます
	 * @throws NativeWholeTextError Llama実装ではcontext作成失敗を取消し又は資源不足として報告します
	 * @pre model/metadataが有効で、既存context/samplerがないこと。語彙専用modelでは呼びません
	 * @post 成功時はownerがcontextを保持し、失敗後もmodel/backendをcloseする責任が残ります
	 * @note Llama実装は一sequence、固定thread数・KV型・batch設定を使い、GPU offloadを無効にします
	 */
	//------------------------------------------------------------------------------
	virtual void				   openContext() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 作成済みcontextが実際に持つtoken位置の容量を取得します
	 * @return Llama実装のllama_n_ctx値です。空き位置数や生成済み件数でなく、単位はtokenです
	 * @pre contextのopenが成功し、closeChunk/close前であること。未open用の失敗値は定義しません
	 * @note callerはGemmaの6912又はPlainの5632との一致とprefix＋生成予算を別に検査します。状態は変更しません
	 */
	//------------------------------------------------------------------------------
	virtual std::uint32_t		   contextTokens() const = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 確率的samplingを行わないgreedy samplerを一つ確保します
	 * @throws NativeWholeTextError Llama実装ではsamplerがNULLなら資源不足です。確保時の標準例外も伝わります
	 * @pre live model/contextがあり、既存samplerがないこと。callerはcontext容量の照合後に呼びます
	 * @post 成功時はownerがsamplerを保持します。失敗してもmodel/contextは解放されずcloseの対象です
	 */
	//------------------------------------------------------------------------------
	virtual void				   openSampler() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一sequenceのtoken列を一度decodeし、contextのKV状態と次sample用logitsを更新します
	 * @param[in] tokens 現語彙内IDの非空列をreturnまで借用します。dataは非NULLで、内容は変更しません
	 * 要素数はGemmaで512以下、Plainで128以下かつcontextのbatch/残容量内です
	 * @return Llama実装はllama_decodeの符号を保持します。0は成功、1はKV slot不足の警告、2はabort、
	 * -1は不正batch、-1未満はfatal errorです。callerは正値の警告も含む全非0を拒否します
	 * @pre tensor付きmodel/contextが生存し、同じcontextへの投入順と位置容量をcallerが管理すること
	 * @post abort/fatal errorでは処理済みubatchがKVに残り得ます。全非0で未変更とは保証しません
	 * @note batchは借用で別解放不要です。callerは前後に停止を検査し、code==2かつ停止時だけ取消しへ分類し、
	 * それ以外の非0は推論失敗として元codeを保持します。再decode・巻戻しはここでは行いません
	 */
	//------------------------------------------------------------------------------
	virtual std::int32_t		   decode( std::span<const std::int32_t> tokens) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 直前のdecodeの最後の出力から全語彙logitsを読み、各floatの有限性を検査します
	 * @throws NativeWholeTextError Llama実装ではlogitsがNULL又は非有限なら推論失敗です
	 * @pre live contextと成功decodeの出力があり、次sample前であること。語彙長はmetadata照合済みとします
	 * @post context内logitsを借用して検査するだけで、外へpointerを返さず、sampleやdecodeを行いません
	 * @note Llama実装はGemmaの262144又はPlainの131072要素を検査します。fakeは検査成否・回数をscript化できます
	 */
	//------------------------------------------------------------------------------
	virtual void				   validateLogits() const = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 検査済みの最後のlogitsからgreedyで一tokenを選び、samplerへacceptします
	 * @return Llama実装のllama_sampler_sample(sampler, context, -1)が返したtoken IDです
	 * statusや文字列ではなく、EOG/制御IDも含み得ます。callerが語彙範囲とEOG/制御属性を検査します
	 * @pre 同modelのvocab/contextとgreedy samplerが生存し、成功decode後にvalidateLogitsが完了していること
	 * 検査からsampleまでdecode/closeを挟みません。失敗を表す専用IDや未open時の戻り値は定義しません
	 * @post 選んだIDはまだKVへdecodeしていません。callerはEOGをdecodeせず、必要な非EOGだけを次に投入します
	 * @note 上流の候補確保時の標準例外等は伝わります。ここではlogitsや取得IDの範囲を再検査しません
	 */
	//------------------------------------------------------------------------------
	virtual std::int32_t		   sampleGreedy() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現語彙のtokenが自然な生成終端EOGかを分類します
	 * @param[in] token 現modelの語彙内の非負token IDです。callerが先に範囲を検査します
	 * @return EOGならtrue、それ以外はfalseです。falseはAPI失敗や本文の妥当性を表しません
	 * @pre metadata照合済みのmodel/vocabが生存していること
	 * @note Llama実装はllama_vocab_is_eogへ委譲し状態を変更しません。callerは固定EOG集合との一致も検査します
	 */
	//------------------------------------------------------------------------------
	virtual bool				   isEog( std::int32_t token) const = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 取得済みsampler・context・model・backendを逆順で終了させます
	 * @return 0は実装が全解放呼出しの完遂を報告した値、非0は実装のcleanup異常codeです
	 * 非0だけでは解放済みと未確認を区別できません。Llama実装は保持中なら固定CLEANUPを返してfreeしません
	 * @pre decode/sampleとcallback利用が終了していること。部分open失敗後にも呼べる実装が必要です
	 * @post Llama実装は各formal void freeのreturn後に所有pointer/初期化状態をクリアし、二度目は再freeしません
	 * @note 例外を送出しません。Llamaの0はvoid APIの完了であり、別のnative成功codeの検査ではありません
	 * V1は非0なら本文を返さず、counterは最初のcleanup codeを保持します。基底に自動解放実装はありません
	 */
	//------------------------------------------------------------------------------
	virtual std::uint32_t		   close() noexcept = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief model/backendを保持したまま、現在chunkのsamplerとcontextを逆順に閉じる拡張点です
	 * @return releasedはchunk解放呼出しの完遂確認、codeは実装のcleanup codeです
	 * callerが成功とするのはreleased==trueかつcode==0だけです。基底既定の{false, 0}は未対応/未確認です
	 * @pre chunkのdecode/sampleが終了していること。V2逐次生成を支える派生はoverrideします
	 * @post Llama実装は保持中なら{false, 0}でfreeせず、それ以外はsampler/contextをfree・NULL化して{true, 0}です
	 * 既にchunk資源がなければ同じ完遂値となります。成功後だけ同modelで次contextを開けます
	 * @note 例外を送出しません。released==falseではcallerがownerを解放不明にしretainUnknownを呼び、再利用を止めます
	 */
	//------------------------------------------------------------------------------
	virtual NativeWholeTextCleanup closeChunk() noexcept
	{
		return {false, 0} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief chunkの解放が未確認になった時、以後の解放を抑止する派生側の保持処理を要求します
	 * @pre closeChunkがreleased==falseを返した後であること。以後は推論や再openを行いません
	 * @post 基底既定はno-opです。Llama実装は保持flagを立て、以後のcloseChunk/closeと派生dtorでfreeしません
	 * @note 例外を送出せず、解放の再確認や資源回収は行いません。解放不明を扱う派生はoverrideが必要です
	 */
	//------------------------------------------------------------------------------
	virtual void retainUnknown() noexcept
	{
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 有効UTF-8だけをPython strip集合で描画します。NULを拒否します
 * @param[in] system system全byte
 * @param[in] user user全byte
 * @return 固定prefix全byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string renderGemma4TwoTextThinking( std::string_view system, std::string_view user) ;
//------------------------------------------------------------------------------
/**
 * @brief DLL内static engineのlogだけを一回discardへ固定します
 */
//------------------------------------------------------------------------------
void					  installWholeTextNativeLogSink() ;
//------------------------------------------------------------------------------
/**
 * @brief 指定した語彙長のlogits全要素が有限かsample前に検査します
 * @param[in] logits 同期中だけ借用するfloat列です。dataは非NULLで、要素数はvocabularyTokensと一致させます
 * @param[in] vocabularyTokens 期待する語彙tokenの総要素数です。既定はK_VOCABULARY_TOKENSで、別方式は対応する語彙長を明示します
 * @throws NativeWholeTextError 非有限/長さ違反
 */
//------------------------------------------------------------------------------
void validateWholeTextNativeLogits( std::span<const float> logits, std::uint32_t vocabularyTokens = K_VOCABULARY_TOKENS) ;
//------------------------------------------------------------------------------
/**
 * @brief 一callの有界生成を行い、全解放後のFinalだけ返します
 * @param[in,out] backend 同期中だけ使う所有資源
 * @param[in] request 不変の借用要求
 * @param[in] maximumBodyBytes host残量
 * @return 完全なFinalと実counter
 * @throws NativeWholeTextError 全体失敗、部分本文は返しません
 * @note module内atomic guardで二件目を待たず拒否します
 */
//------------------------------------------------------------------------------
[[nodiscard]] NativeWholeTextGenerated generateGemma4WholeText( NativeWholeTextBackend	&backend,
															   const KoefudeWtRequestV1 &request,
															   std::uint32_t			 maximumBodyBytes) ;
//------------------------------------------------------------------------------
/**
 * @brief ABI byte借用の構造を検査します
 * @param[in] bytes 入力POD
 * @return 元byte借用
 * @throws NativeWholeTextError 構造違反
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view		   nativeBytes( const KoefudeWtBytesV1 &bytes) ;
/** @brief 一つのvocab-only modelを全候補で再利用する生成なしkernelです */
class NativeWholeTextCounter		   final {
	  public:
	  //------------------------------------------------------------------------------
	  /**
	   * @brief metadata/markerを照合して一回だけvocabを開きます
	   * @param[in,out] backend closeまで生存するowner
	   * @param[in] request 値copyするcallback要求
	   * @param[in] session 非0で再利用しないID
	   * @param[in] deferred native owner登録後のinitializeを選ぶ場合だけtrue
	   */
	  //------------------------------------------------------------------------------
	  NativeWholeTextCounter( NativeWholeTextBackend &backend, const KoefudeWtCounterOpenV2 &request,
									 std::uint64_t session, bool deferred = false) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief vocab-only modelと計数sessionの解放責任を複製しないようcopy構築を禁止します
	   * @note 名前のない第1引数（const NativeWholeTextCounter &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	   */
	  //------------------------------------------------------------------------------
	  NativeWholeTextCounter( const NativeWholeTextCounter &) = delete ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief vocab-only modelと計数sessionの解放責任を複製しないようcopy代入を禁止します
	   * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	   * @note 名前のない第1引数（const NativeWholeTextCounter &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	   */
	  //------------------------------------------------------------------------------
	  NativeWholeTextCounter &operator=( const NativeWholeTextCounter &) = delete ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 残資源を例外なしで解放します
	   */
	  //------------------------------------------------------------------------------
	  ~NativeWholeTextCounter() ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 同じrenderer/vocab/flagsで独立計数します
	   * @param[in] request 同期borrow
	   * @return 実receipt。失敗後は再実行しません
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] WholeTextTokenReceipt	 measure( const KoefudeWtMeasureRequestV2 &request) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief native owner確保後だけvocabを開きます
	   * @param[in] request 同期要求
	   */
	  //------------------------------------------------------------------------------
	  void									 initialize( const KoefudeWtCounterOpenV2 &request) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 同じ計数sessionの封印値を照合し、vocab-only modelをtensor付きCPU modelへ一度だけ昇格します
	   * @param[in] request returnまで借用するV2昇格要求。open/plan/inputのSHA、元byte数、計数回数/総量とrecord/生成件数を同じsessionへ照合します
	   * @throws NativeWholeTextError 未open/失敗済み/昇格済み、競合、要求不整合、停止、再openまたはmetadata/marker不一致です
	   * @note modelの再open以降の例外はfailed状態を記録します。成功時はtensor modelと封印計画を保持し、失敗後もownerにclose責任が残ります
	   */
	  //------------------------------------------------------------------------------
	  void									 promote( const KoefudeWtPromoteRequestV2 &request) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 昇格済み同modelで一coreの封印/計数を再照合し、自然終端とchunk解放を確認して返します
	   * @param[in] request 同期借用するV2要求。昇順core範囲、chunk位置、plan/inputのSHA、expectedReceiptと全prefix byte列を元計画へ照合します
	   * @param[in] maximum 本文の最大byte数。1～65536で、host残容量を超えない値です
	   * @param[out] receipt 正常復帰時の生成counter/hash/範囲を持つ成功receipt。例外時は途中の代入が残り得るため採用しません
	   * @return 独立所有の完成Final本文と実counter。自然終端なし、停止、再照合/推論/解放失敗では部分本文を返しません
	   * @throws NativeWholeTextError 状態/要求/順序/上限違反、競合、取消し、計数不一致、未完、推論またはcleanup失敗です
	   * @note 試行開始後の例外はfailed状態を記録します。解放未確認時はretainUnknownで再freeを抑止し、model/sessionの終了責任をownerに残します
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] NativeWholeTextGenerated generate( const KoefudeWtChunkRequestV2 &request, std::uint32_t maximum,
															  WholeTextBoundedChunkReceipt &receipt) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 全解放を確認できたか読みます
	   * @return 不明でなければtrue
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] bool					 releaseConfirmed() const noexcept
	  {
		  return !unknown_ ;
	  }
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 初回cleanup元codeを読みます
	   * @return 0正常、非0は報告待ち失敗
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] std::uint32_t cleanupCode() const noexcept
	  {
		  return cleanupCode_ ;
	  }
	  //------------------------------------------------------------------------------
	  /**
	   * @brief closeを一回行います
	   * @return 元cleanup符号、二回目0
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] std::uint32_t close() noexcept ;

	  private:
	  /// 同じownerのborrow
	  NativeWholeTextBackend	 &backend_ ;
	  /// pointer先はhostの固定owner
	  KoefudeWtRequestV1		  callbacks_{} ;
	  /// 非0 ID
	  std::uint64_t				  session_ = 0 ;
	  /// 全候補実数
	  std::uint64_t				  sequence_ = 0 ;
	  /// 実tokenizer入力byte
	  std::uint64_t				  measuredBytes_ = 0 ;
	  /// open時の同じmodel path
	  std::wstring				  path_ ;
	  /// open要求の固定束縛
	  WholeTextDigest			  binding_{} ;
	  /// promotionの計画SHA
	  WholeTextDigest			  plan_{} ;
	  /// promotionの入力SHA
	  WholeTextDigest			  input_{} ;
	  /// BOSと四つの公式境界
	  std::array<std::int32_t, 5> plainMarkers_{} ;
	  /// vocabとtensorで照合
	  std::array<std::int32_t, 3> markers_{} ;
	  /// 元tape終端
	  std::uint64_t				  originalBytes_ = 0 ;
	  /// 前の生成core終端
	  std::uint64_t				  lastCoreEnd_ = 0 ;
	  /// 空白を含む件数
	  std::uint32_t				  recordCount_ = 0 ;
	  /// 生成上限
	  std::uint32_t				  generationCount_ = 0 ;
	  /// retryなしの実attempt
	  std::uint32_t				  attempts_ = 0 ;
	  /// 前の生成index
	  std::uint32_t				  lastIndex_ = 0 ;
	  /// 初期化は一回だけ
	  bool						  initialized_ = false ;
	  /// 昇格済み
	  bool						  tensor_ = false ;
	  /// 再freeを禁止する所有不明
	  bool						  unknown_ = false ;
	  /// 最初のcleanup元符号
	  std::uint32_t				  cleanupCode_ = 0 ;
	  /// 全解放済みならfalse
	  bool						  open_ = false ;
	  /// 自動再実行を防止
	  bool						  failed_ = false ;
} ;
} // namespace koefude::whole_text
