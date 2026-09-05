"""Local Czech speech-to-text with local Czech speech output for ESP32."""
from __future__ import annotations

import json
import tempfile
import threading
import wave
import winsound
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from faster_whisper import WhisperModel
from piper import PiperVoice

HOST = "0.0.0.0"
PORT = 8080
MODEL_NAME = "small"
MAX_AUDIO_BYTES = 10 * 1024 * 1024
PROJECT_DIR = Path(__file__).resolve().parent
VOICE_PATH = PROJECT_DIR / "voices" / "sk_SK-lili-medium.onnx"

print(f"Loading Whisper model '{MODEL_NAME}' locally...")
model = WhisperModel(MODEL_NAME, device="cpu", compute_type="int8")
print("Loading local Czech Piper voice...")
voice = PiperVoice.load(str(VOICE_PATH))
speech_lock = threading.Lock()
print(f"Ready: http://{HOST}:{PORT}/transcribe (Czech speech output enabled)")


def speak_locally(text: str) -> None:
    """Speak a confirmed Czech transcription through this PC's default speakers."""
    if not text:
        return
    with speech_lock:
        output_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
                output_path = Path(tmp.name)
            with wave.open(str(output_path), "wb") as wav_file:
                voice.synthesize_wav(text, wav_file)
            winsound.PlaySound(str(output_path), winsound.SND_FILENAME)
        except Exception as exc:
            print(f"TTS error: {exc}")
        finally:
            if output_path:
                output_path.unlink(missing_ok=True)


class SttHandler(BaseHTTPRequestHandler):
    server_version = "LocalCzechSTT/1.1"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"{self.client_address[0]} - {fmt % args}")

    def send_json(self, status: int, payload: dict) -> None:
        encoded = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:
        if self.path == "/health":
            self.send_json(200, {"status": "ok", "model": MODEL_NAME, "tts": "sk_SK-lili-medium"})
        else:
            self.send_json(404, {"error": "Use POST /transcribe with a WAV body."})

    def do_POST(self) -> None:
        if self.path != "/transcribe":
            self.send_json(404, {"error": "Use POST /transcribe."})
            return
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            content_length = 0
        if not 44 <= content_length <= MAX_AUDIO_BYTES:
            self.send_json(400, {"error": "Invalid audio size."})
            return

        audio = self.rfile.read(content_length)
        if len(audio) != content_length:
            self.send_json(400, {"error": "Incomplete audio upload."})
            return

        tmp_path = None
        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
                tmp.write(audio)
                tmp_path = tmp.name
            segments, info = model.transcribe(
                tmp_path,
                language="cs",
                task="transcribe",
                beam_size=5,
                vad_filter=True,
            )
            text = "".join(segment.text for segment in segments).strip()
            self.send_json(200, {
                "text": text,
                "language": info.language,
                "duration_seconds": info.duration,
            })
            threading.Thread(target=speak_locally, args=(text,), daemon=True).start()
        except Exception as exc:
            self.send_json(500, {"error": str(exc)})
        finally:
            if tmp_path:
                Path(tmp_path).unlink(missing_ok=True)


if __name__ == "__main__":
    ThreadingHTTPServer((HOST, PORT), SttHandler).serve_forever()
