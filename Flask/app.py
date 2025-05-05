from flask import Flask, request, jsonify
import pickle
import numpy as np
from datetime import datetime, timedelta
import os
import warnings
import random

# Menekan peringatan tentang feature names
warnings.filterwarnings("ignore", category=UserWarning, message="X does not have valid feature names")

app = Flask(__name__)

# Waktu penyiraman yang realistis (pagi dan sore)
REALISTIC_MORNING_HOURS = [6, 7, 8, 9]  # 6-9 pagi
REALISTIC_EVENING_HOURS = [17, 18, 19]  # 5-7 sore
REALISTIC_WATERING_HOURS = REALISTIC_MORNING_HOURS + REALISTIC_EVENING_HOURS

# Load models
try:
    with open('watering_model.pkl', 'rb') as f:
        watering_model = pickle.load(f)
    
    with open('scaler.pkl', 'rb') as f:
        scaler = pickle.load(f)
    
    # Ganti dengan waktu yang realistis jika file tidak ada atau berisi nilai yang tidak realistis
    try:
        with open('optimal_hours.pkl', 'rb') as f:
            loaded_hours = pickle.load(f)
            
        # Validasi jam optimal yang dimuat
        valid_hours = []
        for hour in loaded_hours:
            if 6 <= hour <= 10 or 16 <= hour <= 19:  # Jam yang masuk akal untuk penyiraman
                valid_hours.append(hour)
        
        # Jika tidak ada jam valid yang dimuat, gunakan default
        optimal_hours = valid_hours if valid_hours else REALISTIC_WATERING_HOURS
    except:
        optimal_hours = REALISTIC_WATERING_HOURS
    
    print(f"Model loaded successfully!")
except Exception as e:
    print(f"Error loading model: {e}")
    watering_model = None
    scaler = None
    optimal_hours = REALISTIC_WATERING_HOURS

# Nama fitur untuk scaler
feature_names = ['hour', 'day', 'month', 'dayofweek', 'air_temp', 'air_humidity', 'soil_humidity']

@app.route('/')
def index():
    return "Flask Server for Plant Watering Prediction is running!"

@app.route('/api/predict', methods=['POST'])
def predict():
    if watering_model is None or scaler is None:
        return jsonify({"error": "Model not loaded"}), 500
    
    # Get data from request
    air_temp = float(request.form.get('air_temp', 0))
    air_humidity = float(request.form.get('air_humidity', 0))
    soil_humidity = float(request.form.get('soil_humidity', 0))
    
    # Get time data from request or use current time
    try:
        hour = int(request.form.get('hour', datetime.now().hour))
        day = int(request.form.get('day', datetime.now().day))
        month = int(request.form.get('month', datetime.now().month))
        dayofweek = int(request.form.get('dayofweek', datetime.now().weekday()))
    except ValueError:
        # Fallback to current time if conversion fails
        now = datetime.now()
        hour = now.hour
        day = now.day
        month = now.month
        dayofweek = now.weekday()
    
    # Prepare features as a dictionary with feature names
    features_dict = {
        'hour': hour,
        'day': day,
        'month': month,
        'dayofweek': dayofweek,
        'air_temp': air_temp,
        'air_humidity': air_humidity,
        'soil_humidity': soil_humidity
    }
    
    # Convert to numpy array in the correct order
    features = np.array([[
        features_dict['hour'],
        features_dict['day'],
        features_dict['month'],
        features_dict['dayofweek'],
        features_dict['air_temp'],
        features_dict['air_humidity'],
        features_dict['soil_humidity']
    ]])
    
    # Scale features
    features_scaled = scaler.transform(features)
    
    # Make prediction
    needs_watering = watering_model.predict(features_scaled)[0]
    watering_proba = watering_model.predict_proba(features_scaled)[0][1]  # Probability of class 1
    
    # Current time for reference
    current_time = datetime.now().replace(hour=hour, minute=0, second=0)
    
    # Pisahkan jam pagi dan sore
    morning_hours = [h for h in optimal_hours if 5 <= h <= 12]
    evening_hours = [h for h in optimal_hours if 16 <= h <= 20]
    
    # Pastikan ada setidaknya satu jam di setiap kategori
    if not morning_hours:
        morning_hours = REALISTIC_MORNING_HOURS
    if not evening_hours:
        evening_hours = REALISTIC_EVENING_HOURS
    
    # Tentukan waktu penyiraman berdasarkan kebutuhan dan waktu saat ini
    if needs_watering:
        # Jika membutuhkan penyiraman, cari waktu terdekat
        next_hour = None
        
        # Cek jam sore hari ini
        for h in sorted(evening_hours):
            if h > hour:
                next_hour = h
                optimal_time = current_time.replace(hour=h)
                break
        
        # Jika tidak ada jam sore yang tersedia, cek jam pagi besok
        if next_hour is None:
            next_hour = morning_hours[0]
            optimal_time = (current_time + timedelta(days=1)).replace(hour=next_hour)
    else:
        # Jika tidak membutuhkan penyiraman, sarankan waktu pagi besok
        next_hour = morning_hours[0]
        optimal_time = (current_time + timedelta(days=1)).replace(hour=next_hour)
    
    # Format optimal time as HH:MM
    optimal_time_str = optimal_time.strftime("%H:%M")
    
    # Alternative times - berikan opsi pagi dan sore
    alternative_times = []
    
    # Tambahkan waktu pagi alternatif jika waktu optimal bukan di pagi
    if optimal_time.hour not in morning_hours:
        morning_alt = random.choice(morning_hours)
        alt_morning = (current_time + timedelta(days=1)).replace(hour=morning_alt)
        alternative_times.append(alt_morning.strftime("%H:%M"))
    else:
        # Jika waktu optimal di pagi, tambahkan waktu pagi lain
        other_morning = [h for h in morning_hours if h != optimal_time.hour]
        if other_morning:
            morning_alt = random.choice(other_morning)
            alt_morning = (current_time + timedelta(days=1)).replace(hour=morning_alt)
            alternative_times.append(alt_morning.strftime("%H:%M"))
    
    # Tambahkan waktu sore jika waktu optimal bukan di sore
    if optimal_time.hour not in evening_hours:
        evening_alt = random.choice(evening_hours)
        # Jika masih ada waktu sore hari ini
        if hour < min(evening_hours):
            alt_evening = current_time.replace(hour=evening_alt)
        else:
            alt_evening = (current_time + timedelta(days=1)).replace(hour=evening_alt)
        alternative_times.append(alt_evening.strftime("%H:%M"))
    else:
        # Jika waktu optimal di sore, tambahkan waktu sore lain
        other_evening = [h for h in evening_hours if h != optimal_time.hour]
        if other_evening:
            evening_alt = random.choice(other_evening)
            if hour < min(evening_hours):
                alt_evening = current_time.replace(hour=evening_alt)
            else:
                alt_evening = (current_time + timedelta(days=1)).replace(hour=evening_alt)
            alternative_times.append(alt_evening.strftime("%H:%M"))
    
    # Function to add random minutes
    def add_random_minutes(time_str):
        hour, minute = map(int, time_str.split(':'))
        minute = random.choice([0, 15, 30, 45])  # Choose from common minute values
        return f"{hour:02d}:{minute:02d}"
    
    optimal_time_str = add_random_minutes(optimal_time_str)
    alternative_times = [add_random_minutes(t) for t in alternative_times]
    
    # Prepare response
    response = {
        "membutuhkan_siram": "Ya" if needs_watering else "Tidak",
        "probabilitas_siram": float(watering_proba),
        "waktu_optimal": optimal_time_str,
        "waktu_alternatif": alternative_times
    }
    
    return jsonify(response)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)