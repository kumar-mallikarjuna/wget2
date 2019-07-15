Generate the root CA key:
`$ certtool --generate-privkey --outfile x509-ca-key.pem --rsa`

Generate the root CA certificate:
```
$ certtool --generate-self-signed --load-privkey x509-ca-key.pem --outfile x509-ca-cert.pem 
Generating a self signed certificate...
Please enter the details of the certificate's distinguished name. Just press enter to ignore a field.
Common name: root
UID: 
Organizational unit name: Wget
Organization name: gnuwget
Locality name: 
State or province name: 
Country name (2 chars): 
Enter the subject's domain component (DC): 
This field should not be used in new certificates.
E-mail: 
Enter the certificate's serial number in decimal (default: 6713808735248893543): 


Activation/Expiration time.
The certificate will expire in (days): -1


Extensions.
Does the certificate belong to an authority? (y/N): y
Path length constraint (decimal, -1 for no constraint): 
Is this a TLS web client certificate? (y/N): 
Will the certificate be used for IPsec IKE operations? (y/N): 
Is this a TLS web server certificate? (y/N): 
Enter a dnsName of the subject of the certificate: 
Enter a URI of the subject of the certificate: 
Enter the IP address of the subject of the certificate: 
Enter the e-mail of the subject of the certificate: 
Will the certificate be used to sign OCSP requests? (y/N): y
Will the certificate be used to sign code? (y/N): 
Will the certificate be used for time stamping? (y/N): y
Will the certificate be used for email protection? (y/N): 
Will the certificate be used to sign other certificates? (y/N): y
Will the certificate be used to sign CRLs? (y/N): y
```
Generate the Intermediate CA key:
`$ certtool --generate-privkey --outfile x509-interm-key.pem --rsa`

Generate Intermediate Certificate Signing Request:
```
$ certtool --generate-request --load-privkey x509-interm-key.pem --outfile x509-interm-cert.csr
Generating a PKCS #10 certificate request...
Common name: Intermediate
Organizational unit name: Wget
Organization name: gnuwget
Locality name:
State or province name:
Country name (2 chars):
Enter the subject's domain component (DC):
UID:
Enter a dnsName of the subject of the certificate:
Enter a URI of the subject of the certificate:
Enter the IP address of the subject of the certificate:
Enter the e-mail of the subject of the certificate:
Enter a challenge password:
Does the certificate belong to an authority? (y/N): y
Path length constraint (decimal, -1 for no constraint):
Will the certificate be used for signing (DHE ciphersuites)? (Y/n):
Will the certificate be used for encryption (RSA ciphersuites)? (Y/n):
Will the certificate be used to sign code? (y/N):
Will the certificate be used for time stamping? (y/N): y
Will the certificate be used for email protection? (y/N):
Will the certificate be used for IPsec IKE operations? (y/N):
Will the certificate be used to sign OCSP requests? (y/N): y
Will the certificate be used to sign other certificates? (y/N): y
Will the certificate be used to sign CRLs? (y/N): y
Is this a TLS web client certificate? (y/N):
Is this a TLS web server certificate? (y/N):
```

Sign Intermediate Certificate:
```
$ certtool --generate-certificate --load-request x509-interm-cert.csr --load-ca-privkey x509-ca-key.pem --load-ca-certificate x509-ca-cert.pem --outfile x509-interm-cert.pem
Generating a signed certificate...
Enter the certificate's serial number in decimal (default: 6713793478692987957): 


Activation/Expiration time.
The certificate will expire in (days): -1


Extensions.
Do you want to honour all the extensions from the request? (y/N): y
Does the certificate belong to an authority? (y/N): y
Is this a TLS web client certificate? (y/N): 
Will the certificate be used for IPsec IKE operations? (y/N): 
Is this a TLS web server certificate? (y/N): 
Enter a dnsName of the subject of the certificate: 
Enter a URI of the subject of the certificate: 
Enter the IP address of the subject of the certificate: 
Enter the e-mail of the subject of the certificate: 
Will the certificate be used for signing (required for TLS)? (Y/n): 
Will the certificate be used for encryption (not required for TLS)? (Y/n): 
Will the certificate be used to sign OCSP requests? (y/N): y
Will the certificate be used to sign code? (y/N): 
Will the certificate be used for time stamping? (y/N): y
Will the certificate be used for email protection? (y/N):
```

Create Index for Signing Certificates:
```
$ mkdir -p demoCA/newcerts
$ touch demoCA/index.txt
$ touch demoCA/index.txt.attr
$ echo '1' > demoCA/serial
```

Create a template configuration file:
`$ cp /etc/ssl/openssl.cnf validation.cnf`
Append the following to it:
```
[ v3_OCSP ]
basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature, keyEncipherment
extendedKeyUsage = OCSPSigning
```

Generate Server Key:
`$ openssl genrsa -out x509-server-key.pem`

Generate Server Certificate:
```
$ openssl req -new -x509 -days 36500 -key x509-server-key.pem -out x509-server-cert.pem -config validation.cnf
You are about to be asked to enter information that will be incorporated
into your certificate request.
What you are about to enter is what is called a Distinguished Name or a DN.
There are quite a few fields but you can leave some blank
For some fields there will be a default value,
If you enter '.', the field will be left blank.
-----
Country Name (2 letter code) [AU]:IN
State or Province Name (full name) [Some-State]:State
Locality Name (eg, city) []:City
Organization Name (eg, company) [Internet Widgits Pty Ltd]:gnuwget
Organizational Unit Name (eg, section) []:Wget
Common Name (e.g. server FQDN or YOUR name) []:localhost
Email Address []:end@localhost
```

Generate Server Certificate Signing Request:
`$ openssl x509 -x509toreq -in x509-server-cert.pem -out x509-server-cert.csr -signkey x509-server-key.pem`

Sign the Server Certificate:
`$ openssl ca -batch -days 36500 -keyfile x509-interm-key.pem -cert x509-interm-cert.pem -policy policy_anything -config validation.cnf -notext -out x509-server-cert.pem -infiles x509-server-cert.csr`

Start OCSP Server:
`$ openssl ocsp -index demoCA/index.txt -port 8080 -rsigner x509-interm-cert.pem -rkey x509-interm-key.pem -CA x509-interm-cert.pem -text -out log.txt`

Save Response:
`$ openssl ocsp -sha256 -CAfile x509-interm-cert.pem -issuer x509-interm-cert.pem -cert x509-server-cert.pem -url http://127.0.0.1:8080 -noverify -resp_text -respout ocsp_stapled_resp.der`

BIBLIOGRAPHY
===
1. https://medium.com/@bhashineen/create-your-own-ocsp-server-ffb212df8e63
2. https://gitlab.com/gnuwget/wget/blob/master/tests/certs/create-certs.sh
3. https://gitlab.com/gnuwget/wget2/blob/master/tests/certs/README
