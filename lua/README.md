To create the CA

```sh
openssl ecparam -genkey -name prime256v1 -out ca.key
```

To create a private key

```sh
openssl ecparam -genkey -name prime256v1 -out device.key
```

To create a CSR

```sh
openssl req -new -SHA256 -key device.key -nodes -out device.csr.
```
