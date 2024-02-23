#pragma once

#include <stddef.h>

#define MAX_LAYERS 128
#define MAX_EXPERTS 64

// How many attention sinks to use for rolling buffer
#define KV_SINKS 2

enum Arch {
	LlamaLike,
	Qwen,
	Phi,
	Mixtral,
	Olmo,
	Gemma,
};

struct Config {
	enum Arch arch;    // model architecture
	int dim;           // transformer dimension
	int hidden_dim;    // for ffn layers
	int head_dim;      // for attention heads; usually dim / n_heads
	int n_layers;      // number of layers
	int n_heads;       // number of query heads
	int n_kv_heads;    // number of key/value heads (can be < query heads because of multiquery)
	int vocab_size;    // vocabulary size, usually 256 (byte-level)
	int seq_len;       // max sequence length
	float rope_theta;  // RoPE theta
	int rotary_dim;    // RoPE rotary dimension (elements after that don't get rotated)
	int n_experts;     // number of experts for MoE models
	int n_experts_ac;  // number of active experts for MoE models
	float norm_eps;    // epsilon for layer normalization
	float embed_scale; // scale factor for token embeddings (useful for tied weights)
};

struct Weights {
	int dbits; // 4 for gf4, 8 for fp8, 16 for fp16; determines type of void* below

	// token embedding table
	void* token_embedding_table; // (vocab_size, dim)
	// weights for norms (ln for phi)
	float* ln_weight[MAX_LAYERS]; // (dim,)
	float* rms_att_weight[MAX_LAYERS]; // (dim) rmsnorm weights
	float* rms_ffn_weight[MAX_LAYERS]; // (dim)
	// weights for matmuls
	void* wq[MAX_LAYERS]; // (dim, n_heads * head_dim)
	void* wk[MAX_LAYERS]; // (dim, n_kv_heads * head_dim)
	void* wv[MAX_LAYERS]; // (dim, n_kv_heads * head_dim)
	void* wo[MAX_LAYERS]; // (n_heads * head_dim, dim)
	// weights for ffn (w3 is absent for phi)
	void* w1[MAX_LAYERS]; // (hidden_dim, dim)
	void* w2[MAX_LAYERS]; // (dim, hidden_dim)
	void* w3[MAX_LAYERS]; // (hidden_dim, dim)
	// final norm (ln for phi)
	float* ln_final_weight; // (dim,)
	float* rms_final_weight; // (dim,)
	// classifier weights for the logits, on the last layer
	void* wcls;
	// biases for qkv (qwen, phi)
	float* bq[MAX_LAYERS]; // (dim)
	float* bk[MAX_LAYERS]; // (dim)
	float* bv[MAX_LAYERS]; // (dim)
	// biases for ffn, cls (phi)
	float* b1[MAX_LAYERS]; // (hidden_dim)
	float* b2[MAX_LAYERS]; // (dim)
	float* bcls;
	// moe gate weights (mixtral)
	void* moegate[MAX_LAYERS]; // (n_experts, dim)
	// moe ffn weights (mixtral)
	void* moew1[MAX_LAYERS][MAX_EXPERTS]; // (hidden_dim, dim)
	void* moew2[MAX_LAYERS][MAX_EXPERTS]; // (dim, hidden_dim)
	void* moew3[MAX_LAYERS][MAX_EXPERTS]; // (hidden_dim, dim)
	void** moewr[MAX_LAYERS][3];          // (n_experts)
};

struct RunState {
	// current wave of activations
	float* x;      // activation at current time stamp (dim,)
	float* xb;     // same, but inside a residual branch (dim,)
	float* xb2;    // an additional buffer just for convenience (dim,)
	float* xa;     // buffer for parallel activation accumulation (dim,)
	float* hb;     // buffer for hidden dimension in the ffn (hidden_dim,)
	float* hb2;    // buffer for hidden dimension in the ffn (hidden_dim,)
	float* he;     // buffer for hidden dimension in the ffn (n_experts_ac,hidden_dim,)
	float* q;      // query (dim,)
	float* k;      // key (dim,)
	float* v;      // value (dim,)
	float* att;    // buffer for scores/attention values (n_heads, seq_len)
	float* exp;    // buffer for MoE computations (n_experts + n_experts_ac * 2)
	float* logits; // output logits
	// kv cache
	int kvbits;        // 8 for fp8, 16 for fp16; determines type of void* below
	void* key_cache;   // (layer, seq_len, dim)
	void* value_cache; // (layer, seq_len, dim)
};

struct Transformer {
	struct Config config;   // the hyperparameters of the architecture (the blueprint)
	struct Weights weights; // the weights of the model
	struct RunState state;  // buffers for the "wave" of activations in the forward pass
	size_t n_params, n_bytes, n_bandwidth;
	float* (*forward)(struct Transformer* transformer, int token, int pos, unsigned flags);
};

enum ForwardFlags {
	FF_UPDATE_KV_ONLY = 1 << 0, // only update kv cache and don't output logits
};
