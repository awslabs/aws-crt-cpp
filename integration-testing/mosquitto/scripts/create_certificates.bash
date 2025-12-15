#!/bin/bash

IP="localhost"
SUBJECT_CA="/C=DE/ST=Hamburg/L=Test/O=ApexCA/OU=CA/CN=localhost"
SUBJECT_SERVER="/C=DE/ST=Munich/L=Test/O=ApexServer/OU=Server/CN=localhost"
SUBJECT_CLIENT="/C=DE/ST=Berlin/L=Test/O=ApexClient/OU=Client/CN=device"

function generate_CA () {
   echo "$SUBJECT_CA"
   openssl req -x509 -nodes -sha256 -newkey rsa:2048 -subj "$SUBJECT_CA"  -days 3650 -keyout ca.key -out ca.crt
   cat ca.crt > ca_certificate.pem
}

function generate_server () {
   echo "$SUBJECT_SERVER"
   openssl req -nodes -sha256 -new -subj "$SUBJECT_SERVER" -keyout server.key -out server.csr
   openssl x509 -req -sha256 -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 3650
   cat server.crt > broker_certificate.pem
}

function generate_client () {
   echo "$SUBJECT_CLIENT"
   openssl req -new -nodes -sha256 -subj "$SUBJECT_CLIENT" -out client.csr -keyout client.key
   openssl x509 -req -sha256 -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -days 3650
   cat client.crt > client_certificate.pem
}

generate_CA
generate_server
generate_client
