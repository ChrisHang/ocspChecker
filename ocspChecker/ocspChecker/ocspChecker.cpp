// openssl110g.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"libssl.lib")
#pragma comment(lib,"libcrypto.lib")

extern "C" {
#include <openssl\applink.c>
};

//#include<tchar.h>
#include<WinSock2.h>
//#include<WS2tcpip.h>
//#include<iostream>
//#include<openssl\ssl.h>

//g++ main.cpp -lcrypto
//#include <openssl\pem.h>
#include <openssl\x509.h>
#include <openssl\x509v3.h>
#include <openssl\ssl.h>
#include <openssl\crypto.h>
#include <openssl\ocsp.h>
#include <openssl\pem.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <string>

using std::cout;
using std::endl;
using std::stringstream;
using std::map;
using std::vector;
using std::string;


//----------------------------------------------------------------------
int prepareRequest(OCSP_REQUEST **req, X509 *cert, const EVP_MD *cert_id_md, X509 *issuer,
	STACK_OF(OCSP_CERTID) *ids)
{
	OCSP_CERTID *id;
	if (!issuer)
	{
		std::cerr << "No issuer certificate specified" << endl;
		//BIO_printf(bio_err, "No issuer certificate specified\n");
		return 0;
	}
	if (!*req) *req = OCSP_REQUEST_new();
	if (!*req) goto err;
	id = OCSP_cert_to_id(cert_id_md, cert, issuer);
	if (!id || !sk_OCSP_CERTID_push(ids, id)) goto err;
	if (!OCSP_request_add0_id(*req, id)) goto err;
	return 1;

err:
	std::cerr << "Error Creating OCSP request" << endl;
	//BIO_printf(bio_err, "Error Creating OCSP request\n");
	return 0;
}
//----------------------------------------------------------------------
OCSP_RESPONSE * queryResponder(BIO *err, BIO *cbio, char *path,
	char *host, OCSP_REQUEST *req, int req_timeout)
{
	int fd;
	int rv;
	int i;
	OCSP_REQ_CTX *ctx = NULL;
	OCSP_RESPONSE *rsp = NULL;
	fd_set confds;
	struct timeval tv;

	if (req_timeout != -1)
		BIO_set_nbio(cbio, 1);

	rv = BIO_do_connect(cbio);

	if ((rv <= 0) && ((req_timeout == -1) || !BIO_should_retry(cbio)))
	{
		std::cerr << "Error connecting BIO" << endl;
		return NULL;
	}

	if (BIO_get_fd(cbio, &fd) <= 0)
	{
		std::cerr << "Can't get connection fd" << endl;
		goto err;
	}

	if (req_timeout != -1 && rv <= 0)
	{
		FD_ZERO(&confds);
		FD_SET(fd, &confds);
		tv.tv_usec = 0;
		tv.tv_sec = req_timeout;
		rv = select(fd + 1, NULL, &confds, NULL, &tv);
		if (rv == 0)
		{
			std::cerr << "Timeout on connect" << endl;
			//BIO_puts(err, "Timeout on connect\n");
			return NULL;
		}
	}

	ctx = OCSP_sendreq_new(cbio, path, NULL, -1);
	if (!ctx)
		return NULL;

	if (!OCSP_REQ_CTX_add1_header(ctx, "Host", host))
		goto err;

	if (!OCSP_REQ_CTX_set1_req(ctx, req))
		goto err;

	for (;;)
	{
		rv = OCSP_sendreq_nbio(&rsp, ctx);
		if (rv != -1)
			break;
		if (req_timeout == -1)
			continue;
		FD_ZERO(&confds);
		FD_SET(fd, &confds);
		tv.tv_usec = 0;
		tv.tv_sec = req_timeout;
		if (BIO_should_read(cbio))
			rv = select(fd + 1, &confds, NULL, NULL, &tv);
		else if (BIO_should_write(cbio))
			rv = select(fd + 1, NULL, &confds, NULL, &tv);
		else
		{
			std::cerr << "Unexpected retry condition" << endl;
			goto err;
		}
		if (rv == 0)
		{
			std::cerr << "Timeout on request" << endl;
			break;
		}
		if (rv == -1)
		{
			std::cerr << "Select error" << endl;
			break;
		}

	}
err:
	if (ctx)
		OCSP_REQ_CTX_free(ctx);

	return rsp;
}
//----------------------------------------------------------------------
OCSP_RESPONSE * sendRequest(BIO *err, OCSP_REQUEST *req,
	char *host, char *path, char *port, int use_ssl,
	int req_timeout)
{
	BIO *cbio = NULL;
	OCSP_RESPONSE *resp = NULL;
	cbio = BIO_new_connect(host);
	if (cbio && port && use_ssl == 0)
	{
		BIO_set_conn_port(cbio, port);
		resp = queryResponder(err, cbio, path, host, req, req_timeout);
		if (!resp)
			std::cerr << "Error querying OCSP responder" << endl;
	}
	if (cbio)
		BIO_free_all(cbio);
	return resp;
}

struct ocsp_response_st {
	ASN1_ENUMERATED *responseStatus;
	OCSP_RESPBYTES *responseBytes;
};
struct ocsp_resp_bytes_st {
	ASN1_OBJECT *responseType;
	ASN1_OCTET_STRING *response;
};
struct ocsp_responder_id_st {
	int type;
	union {
		X509_NAME *byName;
		ASN1_OCTET_STRING *byKey;
	} value;
};
struct ocsp_response_data_st {
	ASN1_INTEGER *version;
	OCSP_RESPID responderId;
	ASN1_GENERALIZEDTIME *producedAt;
	STACK_OF(OCSP_SINGLERESP) *responses;
	STACK_OF(X509_EXTENSION) *responseExtensions;
};
struct ocsp_basic_response_st {
	OCSP_RESPDATA tbsResponseData;
	X509_ALGOR signatureAlgorithm;
	ASN1_BIT_STRING *signature;
	STACK_OF(X509) *certs;
};
struct ocsp_single_response_st {
	OCSP_CERTID *certId;
	OCSP_CERTSTATUS *certStatus;
	ASN1_GENERALIZEDTIME *thisUpdate;
	ASN1_GENERALIZEDTIME *nextUpdate;
	STACK_OF(X509_EXTENSION) *singleExtensions;
};
struct ocsp_cert_status_st {
	int type;
	union {
		ASN1_NULL *good;
		OCSP_REVOKEDINFO *revoked;
		ASN1_NULL *unknown;
	} value;
};
//----------------------------------------------------------------------
int parseResponse(OCSP_RESPONSE *resp)
{

	int is_revoked = -1;
	OCSP_RESPBYTES *rb = resp->responseBytes;
	if (rb && OBJ_obj2nid(rb->responseType) == NID_id_pkix_OCSP_basic)
	{
		OCSP_BASICRESP *br = OCSP_response_get1_basic(resp);
		OCSP_RESPDATA  rd = br->tbsResponseData;

		for (int i = 0; i < sk_OCSP_SINGLERESP_num(rd.responses); i++)
		{
			OCSP_SINGLERESP *single = sk_OCSP_SINGLERESP_value(rd.responses, i);
			OCSP_CERTID *cid = single->certId;
			OCSP_CERTSTATUS *cst = single->certStatus;
			if (cst->type == V_OCSP_CERTSTATUS_REVOKED)
			{
				is_revoked = 1;
			}
			else if (cst->type == V_OCSP_CERTSTATUS_GOOD)
			{
				is_revoked = 0;
			}
		}
		OCSP_BASICRESP_free(br);
	}
	return is_revoked;
}
//----------------------------------------------------------------------
int checkCertOCSP(X509 *x509, X509 *issuer,char *ocsp_url)
{
	int is_revoked = -1;

	BIO *bio_out = BIO_new_fp(stdout, BIO_NOCLOSE | BIO_FP_TEXT);
	BIO *bio_err = BIO_new_fp(stderr, BIO_NOCLOSE | BIO_FP_TEXT);

	if (issuer)
	{
		//build ocsp request
		OCSP_REQUEST *req = NULL;
		STACK_OF(CONF_VALUE) *headers = NULL;
		STACK_OF(OCSP_CERTID) *ids = sk_OCSP_CERTID_new_null();
		const EVP_MD *cert_id_md = EVP_sha1();
		prepareRequest(&req, x509, cert_id_md, issuer, ids);

			char *host = NULL, *port = NULL, *path = NULL;
			int use_ssl, req_timeout = 30;
			
			if (OCSP_parse_url(ocsp_url, &host, &port, &path, &use_ssl) && !use_ssl)
			{
				//send ocsp request
				OCSP_RESPONSE *resp = sendRequest(bio_err, req, host, path, port, use_ssl, req_timeout);
				if (resp)
				{
					//see crypto/ocsp/ocsp_prn.c for examples parsing OCSP responses
					int responder_status = OCSP_response_status(resp);

					//parse response
					if (resp && responder_status == OCSP_RESPONSE_STATUS_SUCCESSFUL)
					{
						is_revoked = parseResponse(resp);
					}
					OCSP_RESPONSE_free(resp);
				}
			}
			OPENSSL_free(host);
			OPENSSL_free(path);
			OPENSSL_free(port);
		OCSP_REQUEST_free(req);
	}

	BIO_free(bio_out);
	BIO_free(bio_err);
	return is_revoked;
}
//----------------------------------------------------------------------
string commonName(X509 *x509)
{
	X509_NAME *subject = X509_get_subject_name(x509);
	int subject_position = X509_NAME_get_index_by_NID(subject, NID_commonName, 0);
	X509_NAME_ENTRY *entry = subject_position == -1 ? NULL : X509_NAME_get_entry(subject, subject_position);
	ASN1_STRING *d = X509_NAME_ENTRY_get_data(entry);
	return string((char*)ASN1_STRING_data(d), ASN1_STRING_length(d));
}
//----------------------------------------------------------------------
void isRevokedByOCSP(const char cert_bytes[], const char issuer_bytes[])
{
	string sUrl = "http://210.74.42.11:8080/ocsp/";
	char cUrl[100];
	strcpy_s(cUrl, sUrl.c_str());
	BIO *bio_mem1 = BIO_new(BIO_s_mem());
	BIO *bio_mem2 = BIO_new(BIO_s_mem());
	BIO_puts(bio_mem1, cert_bytes);
	BIO_puts(bio_mem2, issuer_bytes);
	X509 * x509 = PEM_read_bio_X509(bio_mem1, NULL, NULL, NULL);
	X509 * issuer = PEM_read_bio_X509(bio_mem2, NULL, NULL, NULL);
	cout << "isRevokedByOCSP: " << checkCertOCSP(x509, issuer, cUrl) << endl;
	BIO_free(bio_mem1);
	BIO_free(bio_mem2);
	X509_free(x509);
	X509_free(issuer);
}
//----------------------------------------------------------------------




//----------------------------------------------------------------------
int main(int argc, char **argv)
{
	OpenSSL_add_all_algorithms();

	const char cert1_bytes[] = "-----BEGIN CERTIFICATE-----" "\n"
		"MIIDszCCAzigAwIBAgIQDGv40oFewTIKpCtIVTYSOTAKBggqhkjOPQQDAjBMMQsw" "\n"
		"CQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMSYwJAYDVQQDEx1EaWdp" "\n"
		"Q2VydCBFQ0MgU2VjdXJlIFNlcnZlciBDQTAeFw0xNTA3MjgwMDAwMDBaFw0xNjA5" "\n"
		"MzAxMjAwMDBaMHQxCzAJBgNVBAYTAlVTMREwDwYDVQQIEwhJbGxpbm9pczEQMA4G" "\n"
		"A1UEBxMHQ2hpY2FnbzEoMCYGA1UEChMfWmFja3MgSW52ZXN0bWVudCBSZXNlYXJj" "\n"
		"aCwgSW5jLjEWMBQGA1UEAxMNd3d3LnphY2tzLmNvbTBZMBMGByqGSM49AgEGCCqG" "\n"
		"SM49AwEHA0IABOYOkwbEkkL/xKRUFV8xIfXYm5G/CnwpopbjZaLki/buATo2eSNd" "\n"
		"0gPYzhzrfpd9HWV34Z/kO/yocvpbOTFrNDijggHSMIIBzjAfBgNVHSMEGDAWgBSj" "\n"
		"neYf+do5T8Bu6JHLlaXaMeIKnzAdBgNVHQ4EFgQUtGr+7XN7qK4ZmnEDBNn7V+YI" "\n"
		"QU0wIwYDVR0RBBwwGoINd3d3LnphY2tzLmNvbYIJemFja3MuY29tMA4GA1UdDwEB" "\n"
		"/wQEAwIDiDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwaQYDVR0fBGIw" "\n"
		"YDAuoCygKoYoaHR0cDovL2NybDMuZGlnaWNlcnQuY29tL3NzY2EtZWNjLWcxLmNy" "\n"
		"bDAuoCygKoYoaHR0cDovL2NybDQuZGlnaWNlcnQuY29tL3NzY2EtZWNjLWcxLmNy" "\n"
		"bDBCBgNVHSAEOzA5MDcGCWCGSAGG/WwBATAqMCgGCCsGAQUFBwIBFhxodHRwczov" "\n"
		"L3d3dy5kaWdpY2VydC5jb20vQ1BTMHsGCCsGAQUFBwEBBG8wbTAkBggrBgEFBQcw" "\n"
		"AYYYaHR0cDovL29jc3AuZGlnaWNlcnQuY29tMEUGCCsGAQUFBzAChjlodHRwOi8v" "\n"
		"Y2FjZXJ0cy5kaWdpY2VydC5jb20vRGlnaUNlcnRFQ0NTZWN1cmVTZXJ2ZXJDQS5j" "\n"
		"cnQwDAYDVR0TAQH/BAIwADAKBggqhkjOPQQDAgNpADBmAjEA2vqnY3CyBs18df3H" "\n"
		"h+DJBj8t91Ix6DKdJdrJg/HiPtg9EwQ8TRwZ5Fg4HgTmNaTiAjEAxzYXnrz9tK9N" "\n"
		"DEh5AG+tvna+rzsBwEAh/rBPXeFQx2uCt9deviww57Eg4pSx5cBL" "\n"
		"-----END CERTIFICATE-----" "\n";

	const char issuer1_bytes[] = "-----BEGIN CERTIFICATE-----" "\n"
		"MIIDrDCCApSgAwIBAgIQCssoukZe5TkIdnRw883GEjANBgkqhkiG9w0BAQwFADBh" "\n"
		"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3" "\n"
		"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD" "\n"
		"QTAeFw0xMzAzMDgxMjAwMDBaFw0yMzAzMDgxMjAwMDBaMEwxCzAJBgNVBAYTAlVT" "\n"
		"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxJjAkBgNVBAMTHURpZ2lDZXJ0IEVDQyBT" "\n"
		"ZWN1cmUgU2VydmVyIENBMHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE4ghC6nfYJN6g" "\n"
		"LGSkE85AnCNyqQIKDjc/ITa4jVMU9tWRlUvzlgKNcR7E2Munn17voOZ/WpIRllNv" "\n"
		"68DLP679Wz9HJOeaBy6Wvqgvu1cYr3GkvXg6HuhbPGtkESvMNCuMo4IBITCCAR0w" "\n"
		"EgYDVR0TAQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwNAYIKwYBBQUHAQEE" "\n"
		"KDAmMCQGCCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5jb20wQgYDVR0f" "\n"
		"BDswOTA3oDWgM4YxaHR0cDovL2NybDMuZGlnaWNlcnQuY29tL0RpZ2lDZXJ0R2xv" "\n"
		"YmFsUm9vdENBLmNybDA9BgNVHSAENjA0MDIGBFUdIAAwKjAoBggrBgEFBQcCARYc" "\n"
		"aHR0cHM6Ly93d3cuZGlnaWNlcnQuY29tL0NQUzAdBgNVHQ4EFgQUo53mH/naOU/A" "\n"
		"buiRy5Wl2jHiCp8wHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUwDQYJ" "\n"
		"KoZIhvcNAQEMBQADggEBAMeKoENL7HTJxavVHzA1Nm6YVntIrAVjrnuaVyRXzG/6" "\n"
		"3qttnMe2uuzO58pzZNvfBDcKAEmzP58mrZGMIOgfiA4q+2Y3yDDo0sIkp0VILeoB" "\n"
		"UEoxlBPfjV/aKrtJPGHzecicZpIalir0ezZYoyxBEHQa0+1IttK7igZFcTMQMHp6" "\n"
		"mCHdJLnsnLWSB62DxsRq+HfmNb4TDydkskO/g+l3VtsIh5RHFPVfKK+jaEyDj2D3" "\n"
		"loB5hWp2Jp2VDCADjT7ueihlZGak2YPqmXTNbk19HOuNssWvFhtOyPNV6og4ETQd" "\n"
		"Ea8/B6hPatJ0ES8q/HO3X8IVQwVs1n3aAr0im0/T+Xc=" "\n"
		"-----END CERTIFICATE-----" "\n";

	const char cert2_bytes[] = "-----BEGIN CERTIFICATE-----" "\n"
		"MIIFCjCCA/KgAwIBAgIQdPwLZPmx/EfhJqUTOWRNaTANBgkqhkiG9w0BAQUFADBz" "\n"
		"MQswCQYDVQQGEwJHQjEbMBkGA1UECBMSR3JlYXRlciBNYW5jaGVzdGVyMRAwDgYD" "\n"
		"VQQHEwdTYWxmb3JkMRowGAYDVQQKExFDT01PRE8gQ0EgTGltaXRlZDEZMBcGA1UE" "\n"
		"AxMQUG9zaXRpdmVTU0wgQ0EgMjAeFw0xNDAzMTEwMDAwMDBaFw0xOTAzMTAyMzU5" "\n"
		"NTlaMFYxITAfBgNVBAsTGERvbWFpbiBDb250cm9sIFZhbGlkYXRlZDEUMBIGA1UE" "\n"
		"CxMLUG9zaXRpdmVTU0wxGzAZBgNVBAMTEm96Y29tcHV0ZXJzLmNvbS5hdTCCASIw" "\n"
		"DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMmybPUeo6tsDuuXNJ2uatedHZCR" "\n"
		"duHtcTXTzURJ6yIUdwVJ4gkI+0w/gqVRBEA748M1boYeBWJOeMdyTjpkopqJD49S" "\n"
		"BOSi2MhEEmfXQpKXmFz6n0WKaHnn8Aa1Vlo1+EEzskJ8KFY0j5VEG/itCvBhXd1j" "\n"
		"xN7GzCqh62M7CpIprpfzHXj8cv7HQ1H57mMosLYo6dmBUfDfuBqHxy64vl06B3DI" "\n"
		"Z9+3fZl30PQb2dpHywkAvg1gmI0BhJK+ONdz0oEhniNFS67FMbomNr3VAsy32sNG" "\n"
		"6pIn72r/iynXCt8EeC8HsIzcVZRSnpKWUPBc4qsXh2vXb8Emg6pXm7PDrRECAwEA" "\n"
		"AaOCAbUwggGxMB8GA1UdIwQYMBaAFJnkQF9rFF4+Bdnd02NU/GK49wCsMB0GA1Ud" "\n"
		"DgQWBBTq+Z3M1D1E/IAZSABGYIQwJw5uKzAOBgNVHQ8BAf8EBAMCBaAwDAYDVR0T" "\n"
		"AQH/BAIwADAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwUAYDVR0gBEkw" "\n"
		"RzA7BgsrBgEEAbIxAQICBzAsMCoGCCsGAQUFBwIBFh5odHRwOi8vd3d3LnBvc2l0" "\n"
		"aXZlc3NsLmNvbS9DUFMwCAYGZ4EMAQIBMDsGA1UdHwQ0MDIwMKAuoCyGKmh0dHA6" "\n"
		"Ly9jcmwuY29tb2RvY2EuY29tL1Bvc2l0aXZlU1NMQ0EyLmNybDBsBggrBgEFBQcB" "\n"
		"AQRgMF4wNgYIKwYBBQUHMAKGKmh0dHA6Ly9jcnQuY29tb2RvY2EuY29tL1Bvc2l0" "\n"
		"aXZlU1NMQ0EyLmNydDAkBggrBgEFBQcwAYYYaHR0cDovL29jc3AuY29tb2RvY2Eu" "\n"
		"Y29tMDUGA1UdEQQuMCyCEm96Y29tcHV0ZXJzLmNvbS5hdYIWd3d3Lm96Y29tcHV0" "\n"
		"ZXJzLmNvbS5hdTANBgkqhkiG9w0BAQUFAAOCAQEA45yd7ccirosItEYH9pP/GEBu" "\n"
		"Zllsb3vPTZa/DcEq2hY+Hwuc+Dd6caJp9hwQIEXKgb1CJb1xNpAUnmaeH5jyOhXp" "\n"
		"AobpC/hfstgiiOGrPF2bp2lx33pi6uyLjtPS8WQK1bXJbO8FJoNRZc+nbswU0asq" "\n"
		"HdG4Su9fNjfBs8V4TW9X7kItOqpslNHyaS2TUF1aJWVFcORez5S1O/WiXZMP6yGt" "\n"
		"ars8VZh8eHoy+TSwS7lrjBFEq4QXvaYf1L8RBLWF+uLWk5RJr9UA0H/G+CP+G1Mw" "\n"
		"fHi5+er4glz2RtZFbo1xQjSPzvjBbZ6RPVurMQ+ukKN1G2rMJeBykgB9gl5Tiw==" "\n"
		"-----END CERTIFICATE-----" "\n";

	const char issuer2_bytes[] = "-----BEGIN CERTIFICATE-----" "\n"
		"MIIE5TCCA82gAwIBAgIQB28SRoFFnCjVSNaXxA4AGzANBgkqhkiG9w0BAQUFADBv" "\n"
		"MQswCQYDVQQGEwJTRTEUMBIGA1UEChMLQWRkVHJ1c3QgQUIxJjAkBgNVBAsTHUFk" "\n"
		"ZFRydXN0IEV4dGVybmFsIFRUUCBOZXR3b3JrMSIwIAYDVQQDExlBZGRUcnVzdCBF" "\n"
		"eHRlcm5hbCBDQSBSb290MB4XDTEyMDIxNjAwMDAwMFoXDTIwMDUzMDEwNDgzOFow" "\n"
		"czELMAkGA1UEBhMCR0IxGzAZBgNVBAgTEkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4G" "\n"
		"A1UEBxMHU2FsZm9yZDEaMBgGA1UEChMRQ09NT0RPIENBIExpbWl0ZWQxGTAXBgNV" "\n"
		"BAMTEFBvc2l0aXZlU1NMIENBIDIwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK" "\n"
		"AoIBAQDo6jnjIqaqucQA0OeqZztDB71Pkuu8vgGjQK3g70QotdA6voBUF4V6a4Rs" "\n"
		"NjbloyTi/igBkLzX3Q+5K05IdwVpr95XMLHo+xoD9jxbUx6hAUlocnPWMytDqTcy" "\n"
		"Ug+uJ1YxMGCtyb1zLDnukNh1sCUhYHsqfwL9goUfdE+SNHNcHQCgsMDqmOK+ARRY" "\n"
		"FygiinddUCXNmmym5QzlqyjDsiCJ8AckHpXCLsDl6ez2PRIHSD3SwyNWQezT3zVL" "\n"
		"yOf2hgVSEEOajBd8i6q8eODwRTusgFX+KJPhChFo9FJXb/5IC1tdGmpnc5mCtJ5D" "\n"
		"YD7HWyoSbhruyzmuwzWdqLxdsC/DAgMBAAGjggF3MIIBczAfBgNVHSMEGDAWgBSt" "\n"
		"vZh6NLQm9/rEJlTvA73gJMtUGjAdBgNVHQ4EFgQUmeRAX2sUXj4F2d3TY1T8Yrj3" "\n"
		"AKwwDgYDVR0PAQH/BAQDAgEGMBIGA1UdEwEB/wQIMAYBAf8CAQAwEQYDVR0gBAow" "\n"
		"CDAGBgRVHSAAMEQGA1UdHwQ9MDswOaA3oDWGM2h0dHA6Ly9jcmwudXNlcnRydXN0" "\n"
		"LmNvbS9BZGRUcnVzdEV4dGVybmFsQ0FSb290LmNybDCBswYIKwYBBQUHAQEEgaYw" "\n"
		"gaMwPwYIKwYBBQUHMAKGM2h0dHA6Ly9jcnQudXNlcnRydXN0LmNvbS9BZGRUcnVz" "\n"
		"dEV4dGVybmFsQ0FSb290LnA3YzA5BggrBgEFBQcwAoYtaHR0cDovL2NydC51c2Vy" "\n"
		"dHJ1c3QuY29tL0FkZFRydXN0VVROU0dDQ0EuY3J0MCUGCCsGAQUFBzABhhlodHRw" "\n"
		"Oi8vb2NzcC51c2VydHJ1c3QuY29tMA0GCSqGSIb3DQEBBQUAA4IBAQCcNuNOrvGK" "\n"
		"u2yXjI9LZ9Cf2ISqnyFfNaFbxCtjDei8d12nxDf9Sy2e6B1pocCEzNFti/OBy59L" "\n"
		"dLBJKjHoN0DrH9mXoxoR1Sanbg+61b4s/bSRZNy+OxlQDXqV8wQTqbtHD4tc0azC" "\n"
		"e3chUN1bq+70ptjUSlNrTa24yOfmUlhNQ0zCoiNPDsAgOa/fT0JbHtMJ9BgJWSrZ" "\n"
		"6EoYvzL7+i1ki4fKWyvouAt+vhcSxwOCKa9Yr4WEXT0K3yNRw82vEL+AaXeRCk/l" "\n"
		"uuGtm87fM04wO+mPZn+C+mv626PAcwDj1hKvTfIPWhRRH224hoFiB85ccsJP81cq" "\n"
		"cdnUl4XmGFO3" "\n"
		"-----END CERTIFICATE-----" "\n";
	const char cert3_bytes[] = "-----BEGIN CERTIFICATE-----\n"
		"MIIEMzCCAxugAwIBAgIQEAAAAAAAAAAAAAAgJpMTeTANBgkqhkiG9w0BAQUFADBZ\n"
		"MQswCQYDVQQGEwJDTjEwMC4GA1UEChMnQ2hpbmEgRmluYW5jaWFsIENlcnRpZmlj\n"
		"YXRpb24gQXV0aG9yaXR5MRgwFgYDVQQDEw9DRkNBIFRFU1QgT0NBMTEwHhcNMTgw\n"
		"MTI2MDMxNjM5WhcNMTkwMTI2MTU1OTU5WjB7MQswCQYDVQQGEwJjbjEVMBMGA1UE\n"
		"ChMMQ0ZDQSBURVNUIENBMQ4wDAYDVQQLEwVDTkFQUzESMBAGA1UECxMJQ3VzdG9t\n"
		"ZXJzMTEwLwYDVQQDFCgwNDFAMDIyMDI4MzE5OTIwNjAzMDMyMkB1a2V5end5QDAw\n"
		"MDAwMDAxMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAyKmmfXfNSFCz\n"
		"/MhFnA1JaK9YkQ4a9h6auTRwZXQsWAfh9LoLnxaqDQ91H+/t8w8UkCS4BjkeoNPU\n"
		"Oe93J/MvlN/QG3tCgxB/iX8VUegQPJBzWH+lspCzFG95R3lgDJyv41g6krnHMIrO\n"
		"VmhrqTKoJV9CCaJJuEoYW3uUmGrThq9hDLje285t/p280epw+/RS/anCvXM2PPgu\n"
		"8+wC6JQxmKzEw1sUFfEy3yUqbu7f9NKW40pZqnkQyTYSItmq1QtSWiTLQE+loi72\n"
		"rtASpAJs31oxLJZAFoYP66TzuyVDY8o+FbFwOto/U0wNCOp1jHs3edEISgImPWEq\n"
		"s+S6MyFiOwIDAQABo4HUMIHRMB8GA1UdIwQYMBaAFPwLvESaDjGhg6mBhyceBULG\n"
		"v1b4MAkGA1UdEwQCMAAwOgYDVR0fBDMwMTAvoC2gK4YpaHR0cDovLzIxMC43NC40\n"
		"Mi4zL09DQTExL1JTQS9jcmwyNTI4My5jcmwwCwYDVR0PBAQDAgWgMB0GA1UdDgQW\n"
		"BBSXOI4FWifgSzFVLwrVFdNimhkAFTA7BgNVHSUENDAyBggrBgEFBQcDAgYIKwYB\n"
		"BQUHAwMGCCsGAQUFBwMEBggrBgEFBQcDAQYIKwYBBQUHAwgwDQYJKoZIhvcNAQEF\n"
		"BQADggEBAANx1WvIj8NjeTgIv7nOKDs0iqkpUQRytSrs81gT7vDV/nLNz+NyK1sb\n"
		"xvFfsLdRTi5JbJpfw/dePA7f1Syjeeo9CAz43614bOsCghGbqH7VbKvcwmttEqcz\n"
		"zZKjNqKQCelyHbnfzcOkwqjJPfwyzfTZKmYzHCo4+Po7ygUBL6qLqg+WREc9Hrjq\n"
		"gg9hcjCt+Zc6Bwgvpf+ogb/dTBXtoL5xU8wfFwwHI4Xl0EUDoonpIbwtTn6chrL4\n"
		"7KiPUMKf/4dBC1PhJpAebySf5FSfUs2kQXZjcgsS+ZIWiP+QOBo7nFH+QwaRp+QT\n"
		"IggtF/EVTn0Lc03ZCk2y3S7GLkENRq0=\n"
		"-----END CERTIFICATE-----\n";
	const char issuer3_bytes[] = "-----BEGIN CERTIFICATE-----\n"
		"MIIDzzCCAregAwIBAgIKUalCR1Mt5ZSK8jANBgkqhkiG9w0BAQUFADBZMQswCQYD\n"
		"VQQGEwJDTjEwMC4GA1UEChMnQ2hpbmEgRmluYW5jaWFsIENlcnRpZmljYXRpb24g\n"
		"QXV0aG9yaXR5MRgwFgYDVQQDEw9DRkNBIFRFU1QgQ1MgQ0EwHhcNMTIwODI5MDU1\n"
		"NDM2WhcNMzIwODI0MDU1NDM2WjBZMQswCQYDVQQGEwJDTjEwMC4GA1UEChMnQ2hp\n"
		"bmEgRmluYW5jaWFsIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MRgwFgYDVQQDEw9D\n"
		"RkNBIFRFU1QgT0NBMTEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC8\n"
		"jn0n8Fp6hcRdACdn1+Y6GAkC6KGgNdKyHPrmsdmhCjnd/i4qUFwnG8cp3D4lFw1G\n"
		"jmjSO5yVYbik/NbS6lbNpRgTK3fDfMFvLJpRIC+IFhG9SdAC2hwjsH9qTpL9cK2M\n"
		"bSdrC6pBdDgnbzaM9AGBF4Y6vXhj5nah4ZMsBvDp19LzXjlGuTPLuAgv9ZlWknsd\n"
		"RN70PIAmvomd10uqX4GIJ4Jq/FdKXOLZ2DNK/NhRyN6Yq71L3ham6tutXeZow5t5\n"
		"0254AnUlo1u6SeH9F8itER653o/oMLFwp+63qXAcqrHUlOQPX+JI8fkumSqZ4F2F\n"
		"t/HfVMnqdtFNCnh5+eIBAgMBAAGjgZgwgZUwHwYDVR0jBBgwFoAUdN7FjQp9EBqq\n"
		"aYNbTSHOhpvMcTgwDAYDVR0TBAUwAwEB/zA4BgNVHR8EMTAvMC2gK6AphidodHRw\n"
		"Oi8vMjEwLjc0LjQyLjMvdGVzdHJjYS9SU0EvY3JsMS5jcmwwCwYDVR0PBAQDAgEG\n"
		"MB0GA1UdDgQWBBT8C7xEmg4xoYOpgYcnHgVCxr9W+DANBgkqhkiG9w0BAQUFAAOC\n"
		"AQEAb7W0K9fZPA+JPw6lRiMDaUJ0oh052yEXreMBfoPulxkBj439qombDiFggRLc\n"
		"3g8wIEKzMOzOKXTWtnzYwN3y/JQSuJb/M1QqOEEM2PZwCxI4AkBuH6jg03RjlkHg\n"
		"/kTtuIFp9ItBCC2/KkKlp0ENfn4XgVg2KtAjZ7lpyVU0LPnhEqqUVY/xthjlCSa7\n"
		"/XHNStRxsfCTIBUWJ8n2FZyQhfV/UkMNHDBIiJR0v6C4Ai0/290WvbPEIAq+03Si\n"
		"fsHzBeA0C8lP5VzfAr6wWePaZMCpStpLaoXNcAqReKxQllElOqAhRxC5VKH+rnIQ\n"
		"OMRZvB7FRyE9IfwKApngcZbA5g==\n"
		"-----END CERTIFICATE-----\n";
	const char cert4_bytes[] = "-----BEGIN CERTIFICATE-----\n"
		"MIIEVTCCAz2gAwIBAgIFICaZl5UwDQYJKoZIhvcNAQEFBQAwWTELMAkGA1UEBhMC\n"
		"Q04xMDAuBgNVBAoTJ0NoaW5hIEZpbmFuY2lhbCBDZXJ0aWZpY2F0aW9uIEF1dGhv\n"
		"cml0eTEYMBYGA1UEAxMPQ0ZDQSBURVNUIE9DQTExMB4XDTE4MDIwMjA3NDE1NloX\n"
		"DTE5MDIwMjA3NDE1NlowgYYxCzAJBgNVBAYTAkNOMRcwFQYDVQQKEw5DRkNBIFJT\n"
		"QSBPQ0ExMTETMBEGA1UECxMKQ0ZDQSBPQ0ExMTEVMBMGA1UECxMMSW5kaXZpZHVh\n"
		"bC0xMTIwMAYDVQQDDCkwNTFA546L6L+c6IiqMjAxODAyMDIxNTQxQFoyMDE4MDIw\n"
		"MjE1NDFAMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMjgF61lSDjv\n"
		"34Rn5xM+evz55bPJKlM8kosM+PzeTOnXjIwJ2E9A3Q0/jGmJ49FSv2vCVp2UJ4tv\n"
		"WEpPPjRf48Kz1AJJaf10T5PeFx1VbnNBshLgv4WtOjzQ14z1zkR1DAFHIBE0WWSt\n"
		"7QANHj500eE4rTz/CFPyZKLr5mhA2gksvIqTquncXJK1+jUcBFXP4rk3f5mMRJ5G\n"
		"aD/mcRzUqTc1UpHxFBlsjvf9eKEecYmJFx9c5Iyk6fdB4CfTmfyILLtcszWu9DKx\n"
		"Xo6E17Cyi8j7K+zjP6+VEPoyNjpQ9GSmHfrz4W8T3cZ90wpoKgOKc5vSBHFQR1EZ\n"
		"0nxulqcj6OkCAwEAAaOB9TCB8jAfBgNVHSMEGDAWgBT8C7xEmg4xoYOpgYcnHgVC\n"
		"xr9W+DBIBgNVHSAEQTA/MD0GCGCBHIbvKgECMDEwLwYIKwYBBQUHAgEWI2h0dHA6\n"
		"Ly93d3cuY2ZjYS5jb20uY24vdXMvdXMtMTUuaHRtMDoGA1UdHwQzMDEwL6AtoCuG\n"
		"KWh0dHA6Ly8yMTAuNzQuNDIuMy9PQ0ExMS9SU0EvY3JsMjUzNjIuY3JsMAsGA1Ud\n"
		"DwQEAwID6DAdBgNVHQ4EFgQUytFPU1gp5hanaAYfgpI2ukU3o68wHQYDVR0lBBYw\n"
		"FAYIKwYBBQUHAwIGCCsGAQUFBwMEMA0GCSqGSIb3DQEBBQUAA4IBAQCPkxVv85pS\n"
		"xxVIgqSMG3ax6eguHh90S8qCgeU9/HClvtR+ZJ5tV5r/3Q/MxG6U2RfZXEBJemKY\n"
		"b7VXSlyBITCAuC9984V9/cExXlLU/WRKGzo6Kj0SOdPDQhltpPdvnGIGIe4V6mmy\n"
		"DEaBuLs0kBhk0tJh+drOSSX4k52LQW40YbzBC0xmgvExDsax+8MBdoaVcJOSFlyp\n"
		"UaWcBSFCiPLnzF6T6+AzcoVx/AgiZ8uSb2CYvkYfcSlrxTv7+dmVE5Fu7o2B0bxU\n"
		"8LRfQhfBVtcjOpKTuV2EuqIgBw8BIjUtRBsKjcgRy6CipQ1LjLUgG146M3cgmrUz\n"
		"E8Ke/A6glasx\n"
		"-----END CERTIFICATE-----\n";
	const char issuer4_bytes[] = "-----BEGIN CERTIFICATE-----\n"
		"MIIDzzCCAregAwIBAgIKUalCR1Mt5ZSK8jANBgkqhkiG9w0BAQUFADBZMQswCQYD\n"
		"VQQGEwJDTjEwMC4GA1UEChMnQ2hpbmEgRmluYW5jaWFsIENlcnRpZmljYXRpb24g\n"
		"QXV0aG9yaXR5MRgwFgYDVQQDEw9DRkNBIFRFU1QgQ1MgQ0EwHhcNMTIwODI5MDU1\n"
		"NDM2WhcNMzIwODI0MDU1NDM2WjBZMQswCQYDVQQGEwJDTjEwMC4GA1UEChMnQ2hp\n"
		"bmEgRmluYW5jaWFsIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MRgwFgYDVQQDEw9D\n"
		"RkNBIFRFU1QgT0NBMTEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC8\n"
		"jn0n8Fp6hcRdACdn1+Y6GAkC6KGgNdKyHPrmsdmhCjnd/i4qUFwnG8cp3D4lFw1G\n"
		"jmjSO5yVYbik/NbS6lbNpRgTK3fDfMFvLJpRIC+IFhG9SdAC2hwjsH9qTpL9cK2M\n"
		"bSdrC6pBdDgnbzaM9AGBF4Y6vXhj5nah4ZMsBvDp19LzXjlGuTPLuAgv9ZlWknsd\n"
		"RN70PIAmvomd10uqX4GIJ4Jq/FdKXOLZ2DNK/NhRyN6Yq71L3ham6tutXeZow5t5\n"
		"0254AnUlo1u6SeH9F8itER653o/oMLFwp+63qXAcqrHUlOQPX+JI8fkumSqZ4F2F\n"
		"t/HfVMnqdtFNCnh5+eIBAgMBAAGjgZgwgZUwHwYDVR0jBBgwFoAUdN7FjQp9EBqq\n"
		"aYNbTSHOhpvMcTgwDAYDVR0TBAUwAwEB/zA4BgNVHR8EMTAvMC2gK6AphidodHRw\n"
		"Oi8vMjEwLjc0LjQyLjMvdGVzdHJjYS9SU0EvY3JsMS5jcmwwCwYDVR0PBAQDAgEG\n"
		"MB0GA1UdDgQWBBT8C7xEmg4xoYOpgYcnHgVCxr9W+DANBgkqhkiG9w0BAQUFAAOC\n"
		"AQEAb7W0K9fZPA+JPw6lRiMDaUJ0oh052yEXreMBfoPulxkBj439qombDiFggRLc\n"
		"3g8wIEKzMOzOKXTWtnzYwN3y/JQSuJb/M1QqOEEM2PZwCxI4AkBuH6jg03RjlkHg\n"
		"/kTtuIFp9ItBCC2/KkKlp0ENfn4XgVg2KtAjZ7lpyVU0LPnhEqqUVY/xthjlCSa7\n"
		"/XHNStRxsfCTIBUWJ8n2FZyQhfV/UkMNHDBIiJR0v6C4Ai0/290WvbPEIAq+03Si\n"
		"fsHzBeA0C8lP5VzfAr6wWePaZMCpStpLaoXNcAqReKxQllElOqAhRxC5VKH+rnIQ\n"
		"OMRZvB7FRyE9IfwKApngcZbA5g==\n"
		"-----END CERTIFICATE-----\n";

	//	isRevokedByOCSP(cert1_bytes, issuer1_bytes);
	//	isRevokedByOCSP(cert2_bytes, issuer2_bytes);
	isRevokedByOCSP(cert3_bytes, issuer3_bytes);
	isRevokedByOCSP(cert4_bytes, issuer4_bytes);

	getchar();
}
//----------------------------------------------------------------------