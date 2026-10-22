    // empty: AES-128-GCM, aad 0 bytes, plaintext 0 bytes
    static const char* kBlob_empty =
            "07305982abd4fd264f78a1ca67773d7620184c487c79b285cb9e0f6a";
    static const char* kKey_empty = "092e53789dc2e70c31567ba0c5ea0f34";
    static const char* kAad_empty = "";
    static const char* kPlain_empty = "";

    // oneBlock: AES-128-GCM, aad 0 bytes, plaintext 16 bytes
    static const char* kBlob_oneBlock =
            "07305982abd4fd264f78a1cafdcbe9d12cbacf4fb22e03a1d359fe49d5679306"
            "a4f6f015cfe4d9bddf01254a";
    static const char* kKey_oneBlock = "092e53789dc2e70c31567ba0c5ea0f34";
    static const char* kAad_oneBlock = "";
    static const char* kPlain_oneBlock = "05121f2c394653606d7a8794a1aebbc8";

    // withAad: AES-128-GCM, aad 20 bytes, plaintext 60 bytes
    static const char* kBlob_withAad =
            "07305982abd4fd264f78a1cafdcbe9d12cbacf4fb22e03a1d359fe49ff7728c9"
            "8fe6403429cfb94faa95b0f96f0877401b062edcac8d26ed4029d794b826778b"
            "1beda45e155337c984e2fe840de8cabe0f5205b7abdfd4b2";
    static const char* kKey_withAad = "092e53789dc2e70c31567ba0c5ea0f34";
    static const char* kAad_withAad = "030a11181f262d343b424950575e656c737a8188";
    static const char* kPlain_withAad = "05121f2c394653606d7a8794a1aebbc8d5e2effc091623303d4a5764717e8b98a5b2bfccd9e6f3000d1a2734414e5b6875828f9ca9b6c3d0ddeaf704";

    // partial: AES-128-GCM, aad 3 bytes, plaintext 37 bytes
    static const char* kBlob_partial =
            "07305982abd4fd264f78a1cafdcbe9d12cbacf4fb22e03a1d359fe49ff7728c9"
            "8fe6403429cfb94faa95b0f96f0877401ba532f053cbeba28953cf0056f9f44f"
            "e2";
    static const char* kKey_partial = "092e53789dc2e70c31567ba0c5ea0f34";
    static const char* kAad_partial = "030a11";
    static const char* kPlain_partial = "05121f2c394653606d7a8794a1aebbc8d5e2effc091623303d4a5764717e8b98a5b2bfccd9";

    // aes256: AES-256-GCM, aad 8 bytes, plaintext 48 bytes
    static const char* kBlob_aes256 =
            "07305982abd4fd264f78a1ca01cd08448bd19e3273f00935009969f02a1fae70"
            "f7d9fa84ac7f18c73ba736619be0aa972867cbce570cce96ac74508b7b33b1bd"
            "621559255979d0ef58fb593d";
    static const char* kKey_aes256 = "092e53789dc2e70c31567ba0c5ea0f34597ea3c8ed12375c81a6cbf0153a5f84";
    static const char* kAad_aes256 = "030a11181f262d34";
    static const char* kPlain_aes256 = "05121f2c394653606d7a8794a1aebbc8d5e2effc091623303d4a5764717e8b98a5b2bfccd9e6f3000d1a2734414e5b68";

