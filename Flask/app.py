from flask import Flask, request, jsonify
import pickle, numpy as np
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# Load model & scaler (tiga fitur: [kelembapan udara, suhu, kelembaban tanah])
with open('model.pkl', 'rb') as f:
    model = pickle.load(f)
with open('scaler.pkl', 'rb') as f:
    scaler = pickle.load(f)

@app.route('/predict', methods=['POST'])
def predict():
    try:
        suhu = float(request.json['suhu'])                         # suhu udara
        kelembapan = float(request.json['kelembapan udara'])       # kelembapan udara
        kelembaban_tanah = int(request.json['kelembaban tanah'])  # kelembaban 
    except (KeyError, TypeError, ValueError):
        return jsonify({'error': 'Input tidak valid'}), 400

    # Susun input untuk model (3 fitur: kelembapan udara, suhu, kelembaban tanah)
    X = np.array([[kelembapan, suhu, kelembaban_tanah]])
    Xs = scaler.transform(X)
    y = model.predict(Xs)[0]
    butuh = "Ya" if y < 0.5 else "Tidak"

    # Log
    print("=== Data Masuk ===")
    print(f"  Suhu                 : {suhu} °C")
    print(f"  Kelembapan udara     : {kelembapan} %")
    print(f"  Kelembaban tanah     : {kelembaban_tanah}")
    print(f"  Input setelah scaling: {Xs[0]}")
    print(f"  Prediksi kelembapan tanah (Model): {y:.4f}")
    print(f"  Hasil Prediksi       : {'BUTUH SIRAM' if butuh == 'Ya' else 'TIDAK BUTUH SIRAM'}")
    print("==================\n")

    return jsonify({
        'prediksi_moisture': float(y),
        'butuh_siram': butuh
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
