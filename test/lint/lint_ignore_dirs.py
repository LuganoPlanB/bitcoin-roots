SHARED_EXCLUDED_SUBTREES = ["src/leveldb/",
                 "src/crc32c/",
                 "src/secp256k1/",
                 "src/minisketch/",
                 # VitePress follows this documentation mirror itself. Generic
                 # repository linters must not recurse through its symlinks.
                 "doc/website/content/",
                ]
