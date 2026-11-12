"""Servidor local que imita o serviço de IA, para testar a chamada HTTP do plugin sem rede nem chave.

Uso: python tests/servico_de_mentira.py <porta> <arquivo_com_a_cadeia.json>

Confere os cabeçalhos e o corpo do pedido. Se estiver tudo certo, responde no
formato da API com a cadeia do arquivo; senão, responde 400 dizendo o que faltou.
"""

import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

CHAIN = open(sys.argv[2], encoding="utf-8").read()


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        missing = []
        try:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode("utf-8"))
        except ValueError:
            body, missing = {}, ["corpo em JSON"]
        if self.headers.get("x-api-key") != "chave-de-teste":
            missing.append("x-api-key")
        if self.headers.get("anthropic-version") != "2026-10-21":
            missing.append("anthropic-version")
        if "application/json" not in self.headers.get("Content-Type", ""):
            missing.append("Content-Type")
        if not body.get("model") or not body.get("max_tokens"):
            missing.append("model/max_tokens")
        output_format = body.get("output_config", {}).get("format", {})
        if output_format.get("type") != "json_schema" or "modules" not in output_format.get("schema", {}).get("properties", {}):
            missing.append("output_config.format")
        if "highpass" not in json.dumps(body.get("system", "")):
            missing.append("system com o catálogo")
        messages = body.get("messages") or [{}]
        if "<pedido>" not in str(messages[0].get("content", "")):
            missing.append("messages com o pedido")

        if missing:
            status = 400
            payload = {"type": "error", "error": {"type": "invalid_request_error", "message": "faltou: " + ", ".join(missing)}}
        else:
            status = 200
            payload = {"type": "message", "role": "assistant", "stop_reason": "end_turn",
                       "content": [{"type": "text", "text": CHAIN}]}
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


HTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
