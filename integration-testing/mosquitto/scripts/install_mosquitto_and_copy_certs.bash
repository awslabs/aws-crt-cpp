#!/bin/bash

# install mosquitto
sudo apt update
sudo apt install -y mosquitto

#Copy generated broker certificates
sudo cp broker_* /etc/mosquitto/certs/
sudo cp ca_certificate.pem /etc/mosquitto/ca_certificates/
sudo cp mosquitto.conf /etc/mosquitto/conf.d/

#Copy generated client certifications
cp client_* /tmp/
cp ca_certificate.pem /tmp/

#Run mosquitto
mosquitto -c /etc/mosquitto/conf.d/mosquitto.conf