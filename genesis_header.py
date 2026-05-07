import nacl.signing
import nacl.encoding

def generate_genesis_txs():
    system_private_key = nacl.signing.SigningKey.generate()
    system_public_key = system_private_key.verify_key

    pub_key_hex = system_public_key.encode(encoder=nacl.encoding.HexEncoder).decode('utf-8')
    priv_key_hex = system_private_key.encode(encoder=nacl.encoding.HexEncoder).decode('utf-8')


    backers= ["iudah", "ajmilk", "norwey", "gokayup", "defun", "uthysel", "ricodee", "wloner"]
    genesis_txs = []

    for backer in backers:
        unsigned_str = f'{{"from":"SYSTEM","to":"{backer}","value":350000.0000000000,"for":"genesis allocation","reward":true}}'
        
        signed_bytes = system_private_key.sign(unsigned_str.encode('utf-8'))
        signature_hex = signed_bytes.signature.hex()
        
        tx_json = f"""      "    {{"
      "      \\"from\\": \\"SYSTEM\\","
      "      \\"to\\": \\"{backer}\\","
      "      \\"value\\": 350000.0000000000,"
      "      \\"for\\": \\"genesis allocation\\","
      "      \\"reward\\": true,"
      "      \\"public_key\\": \\"{pub_key_hex}\\","
      "      \\"signature\\": \\"{signature_hex}\\""
      "    }}" """
        genesis_txs.append(tx_json)

    return f""""  \\"txs\\": ["
{"\",\"\n".join(genesis_txs)}
      "  ]" """

with open("src/genesis_header.h", "w") as file:
    header = f"""
#ifndef GENESIS_HEADER_H
#define GENESIS_HEADER_H


const char *genesis_json =
      "{{"
      "  \\"parent_hash\\": \\"0000000000000000000000000000000000000000000000000000000000000000\\","
      "  \\"block_time\\": \\"2024-08-15T00:00:00\\","
      "  \\"block_index\\": 0,"
      {generate_genesis_txs()}
      "}}";

#endif
"""

    file.write(header)
