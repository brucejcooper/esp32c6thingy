To create the CA

```sh
openssl ecparam -genkey -name prime256v1 -out ca.key
```

To create a private key

```sh
openssl ecparam -genkey -name prime256v1 -out devices/$IP/cert.key
```

To create a signed certificate

```sh
openssl req -new -config openssl.conf  -key devices/$IP/cert.key -x509 -nodes -days 3650 -out devices/$IP/cert.pem
```
