#include <new>
#include "jwtpp.h"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <openssl/pem.h>

jwt::~jwt()
{
}

int jwt::jwt_decode(const std::string &token, const std::string &key)
{
	std::string head;
	std::string body;
	std::string sig;

	size_t p1 = token.find_first_of('.');
	if (p1 == std::string::npos)
		return 0;

	head = token.substr(0, p1);

	p1++;
	size_t p2 = token.find_first_of('.', p1);
	if (p2 == std::string::npos)
		return 0;	
	body = token.substr(p1, p2 - p1);

	p2++;
	sig = token.substr(p2);
	if (sig.length() == 0)
		return 0;

	// Now that we have everything split up, let's check out the header.
	this->key = key;
	
	int ret = jwt_verify_head(head);
	if (ret)
		return 0;
	
	ret = jwt_parse_body(body);
	if (ret)
		return 0;

	/* Back up a bit so check the sig if needed. */
	if (alg != JWT_ALG_NONE) {
		BIO *bmem, *b64;
		std::string buf;
		int len;

		b64 = BIO_new(BIO_f_base64());
		bmem = BIO_new(BIO_s_mem());
		if (!b64 || !bmem) {
			throw std::bad_alloc();
		}

		BIO_push(b64, bmem);
		BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
		
		std::string headerpayload;
		headerpayload = head + "." + body;
		ret = jwt_sign(b64, headerpayload);
		if (ret)
			return 0;

		len = BIO_pending(bmem);
		if (len < 0) {
			ret = EINVAL;
			return 0;
		}

		buf.resize(len);

		len = BIO_read(bmem, &buf[0], len);
		BIO_free_all(b64);		
		buf.resize(len);

		base64uri_encode(buf);

		// same?
		ret = (buf == sig) ? 0 : EINVAL;
	}
	else {
		ret = 0;
	}

	return ret;
}


std::string jwt::jwt_get_grant(const std::string &grant)
{
	if (grant.length() == 0)
		return 0;

	return grants.get<std::string>(grant);
}

long jwt::jwt_get_grant_int(const std::string &grant)
{
	if (grant.length() == 0)
		return 0;
		
	return grants.get<int>(grant);
}

int jwt::jwt_add_grant(const std::string &grant, const std::string &val)
{
	if(grant.length() == 0)
		return EINVAL;

	grants.add(grant, val);
	
	return 0;
}

int jwt::jwt_add_grant_int(const std::string &grant, long val)
{
	if (grant.length() == 0)
		return EINVAL;

	grants.add(grant, val);

	return 0;
}

int jwt::jwt_del_grant(const std::string &grant)
{
	if (grant.length() == 0)
		return EINVAL;

	grants.erase(grant);

	return 0;
}

std::string jwt::jwt_encode_str()
{
	BIO *bmem = BIO_new(BIO_s_mem());
	std::string str;
	int len;

	if (!bmem) {
		throw std::bad_alloc();
	}

	errno = jwt_encode_bio(bmem);
	if (errno)
		goto encode_str_done;

	len = BIO_pending(bmem);
	str.resize(len);
	
	len = BIO_read(bmem, &str[0], len);	
	str.resize(len);

encode_str_done:
	BIO_free_all(bmem);

	return str;
}

int jwt::jwt_set_alg(jwt_alg_t alg, const std::string &key)
{
	// No matter what happens here, we do this.
	// jwt_scrub_key(jwt);

	if (alg < JWT_ALG_NONE || alg >= JWT_ALG_TERM)
		return EINVAL;

	switch (alg) {
	case JWT_ALG_NONE:
		if (key.length() > 0)
			return EINVAL;
		break;

	default:
		if (key.length() <= 0)
			return EINVAL;		
	}

	this->alg = alg;
	this->key = key;

	return 0;
}

jwt_alg_t jwt::jwt_get_alg()
{
	return alg;
}

int jwt::jwt_verify_head(const std::string &head)
{
	boost::property_tree::ptree js;
	std::string val;
	int ret;

	js = jwt_b64_decode(head);
	
	val = js.get<std::string>("alg");
	ret = jwt_str_alg(val);
	if (ret)
		goto verify_head_done;

	/* If alg is not NONE, there should be a typ. */
	if (alg != JWT_ALG_NONE) {
		val = js.get<std::string>("typ");
		if (val != "JWT")
			ret = EINVAL;

		if (key.length() <= 0) {			
				ret = EINVAL;
		}
		else {
			// jwt_scrub_key();
		}
	}
	else {
		/* If alg is NONE, there should not be a key */
		if (key.length()){
			ret = EINVAL;
		}
	}

verify_head_done:
	
	return ret;
}

int jwt::jwt_parse_body(const std::string &body)
{
	grants = jwt_b64_decode(body);
	
	return 0;
}

int jwt::jwt_sign(BIO *out, const std::string &str)
{
	switch (alg) {
	case JWT_ALG_HS256:
		return jwt_sign_sha_hmac(out, EVP_sha256(), str.c_str());
	case JWT_ALG_HS384:
		return jwt_sign_sha_hmac(out, EVP_sha384(), str.c_str());
	case JWT_ALG_HS512:
		return jwt_sign_sha_hmac(out, EVP_sha512(), str.c_str());
	case JWT_ALG_RS256:
	case JWT_ALG_ES256:
		return jwt_sign_sha_pem(out, EVP_sha256(), str.c_str());
	case JWT_ALG_RS384:
	case JWT_ALG_ES384:
		return jwt_sign_sha_pem(out, EVP_sha384(), str.c_str());
	case JWT_ALG_RS512:
	case JWT_ALG_ES512:
		return jwt_sign_sha_pem(out, EVP_sha512(), str.c_str());
	default:
		return EINVAL; // LCOV_EXCL_LINE
	}
}

void jwt::base64uri_encode(std::string &str)
{	
	// change symbols and also remove '='
	for (int i = 0; i < str.length(); i++) {
		if (str[i] == '+') {
			str[i] = '-';
		}
		else if (str[i] == '/') {
			str[i] = '_';
		}
		else if (str[i] == '=') {
			str.resize(i);
			return;
		}		
	}
}

int jwt::jwt_sign_sha_hmac(BIO *out, const EVP_MD *alg, const std::string &str)
{
	unsigned char res[EVP_MAX_MD_SIZE];
	unsigned int res_len;

	HMAC(alg, key.c_str(), key.length(),
		reinterpret_cast<const unsigned char *>(str.c_str()), str.length(), res, &res_len);

	BIO_write(out, res, res_len);

	BIO_flush(out);

	return 0;
}

int jwt::jwt_sign_sha_pem(BIO *out, const EVP_MD *alg, const std::string &str)
{	
	EVP_MD_CTX *mdctx = NULL;	
	BIO *bufkey = NULL;
	EVP_PKEY *pkey = NULL;
	std::string sig;
	int ret = EINVAL;
	size_t slen;

	bufkey = BIO_new_mem_buf(reinterpret_cast<const unsigned char *>(key.c_str()), key.length());
	if (bufkey == NULL) {
		ret = ENOMEM;
		goto jwt_sign_sha_pem_done;
	}

	/* This uses OpenSSL's default passphrase callback if needed. The
	* library caller can override this in many ways, all of which are
	* outside of the scope of LibJWT and this is documented in jwt.h. */
	pkey = PEM_read_bio_PrivateKey(bufkey, NULL, NULL, NULL);
	if (pkey == NULL)
		goto jwt_sign_sha_pem_done;

	mdctx = EVP_MD_CTX_create();
	if (mdctx == NULL) {
		return ENOMEM;
		goto jwt_sign_sha_pem_done;
	}

	/* Initialize the DigestSign operation using alg */
	if (EVP_DigestSignInit(mdctx, NULL, alg, NULL, pkey) != 1)
		goto jwt_sign_sha_pem_done;

	/* Call update with the message */
	if (EVP_DigestSignUpdate(mdctx, str.c_str(), str.length()) != 1)
		goto jwt_sign_sha_pem_done;

	/* First, call EVP_DigestSignFinal with a NULL sig parameter to get length
	* of sig. Length is returned in slen */
	if (EVP_DigestSignFinal(mdctx, NULL, &slen) != 1)
		goto jwt_sign_sha_pem_done;

	/* Allocate memory for signature based on returned size */	
	sig.resize(slen);
	
	/* Get the signature */
	if (EVP_DigestSignFinal(mdctx, reinterpret_cast<unsigned char *>(&sig[0]), &slen) == 1) {
		BIO_write(out, sig.c_str(), slen);
		BIO_flush(out);

		ret = 0;
	}

jwt_sign_sha_pem_done:
	if (bufkey)
		BIO_free(bufkey);
	if (pkey)
		EVP_PKEY_free(pkey);
	if (mdctx)
		EVP_MD_CTX_destroy(mdctx);	

	return ret;
}

int jwt::jwt_str_alg(const std::string &alg)
{
	if (alg == "none")
		this->alg = JWT_ALG_NONE;
	else if (alg == "HS256")
		this->alg = JWT_ALG_HS256;
	else if (alg == "HS384")
		this->alg = JWT_ALG_HS384;
	else if (alg == "HS512")
		this->alg = JWT_ALG_HS512;
	else if (alg == "RS256")
		this->alg = JWT_ALG_RS256;
	else if (alg == "RS384")
		this->alg = JWT_ALG_RS384;
	else if (alg == "RS512")
		this->alg = JWT_ALG_RS512;
	else if (alg == "ES256")
		this->alg = JWT_ALG_ES256;
	else if (alg == "ES384")
		this->alg = JWT_ALG_ES384;
	else if (alg == "ES512")
		this->alg = JWT_ALG_ES512;
	else
		return EINVAL;

	return 0;
}

boost::property_tree::ptree jwt::jwt_b64_decode(std::string src)
{
	BIO *b64, *bmem;
	std::string buf;
	int len, i, z;

	// Decode based on RFC-4648 URI safe encoding.	

	for (i = 0; i < src.length(); i++) {
		switch (src[i]) {
		case '-':
			src[i] = '+';
			break;
		case '_':
			src[i] = '/';
			break;
		}
	}
	z = 4 - (i % 4);
	if (z < 4) {
		while (z--)
			src += '=';
	}

	// Setup the OpenSSL base64 decoder.
	b64 = BIO_new(BIO_f_base64());
	bmem = BIO_new_mem_buf(src.c_str(), src.length());
	if (!b64 || !bmem) {
		return boost::property_tree::ptree(); // LCOV_EXCL_LINE
	}

	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	BIO_push(b64, bmem);

	len = BIO_pending(b64);
	if (len <= 0) {
		BIO_free_all(b64);
		return boost::property_tree::ptree();
	}

	buf.resize(len);
	
	len = BIO_read(b64, &buf[0], len);
	BIO_free_all(b64);
	buf.resize(len);

	boost::property_tree::ptree result;
	std::stringstream strstm(buf);
	boost::property_tree::read_json(strstm, result);
	return result;
}

int jwt::jwt_encode_bio(BIO *out)
{
	BIO *b64, *bmem;
	std::string buf;
	int len, len2, ret;

	/* Setup the OpenSSL base64 encoder. */
	b64 = BIO_new(BIO_f_base64());
	bmem = BIO_new(BIO_s_mem());
	if (!b64 || !bmem) {
		throw std::bad_alloc();
	}

	BIO_push(b64, bmem);
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

	// First the header.
	jwt_write_bio_head(b64, false);

	BIO_puts(bmem, ".");

	// Now the body. 
	jwt_write_bio_body(b64, false);

	len = BIO_pending(bmem);
	buf.resize(len);
	
	len = BIO_read(bmem, &buf[0], len);	
	buf.resize(len);

	base64uri_encode(buf);

	BIO_puts(out, buf.c_str());
	BIO_puts(out, ".");

	if (alg == JWT_ALG_NONE)
		goto encode_bio_success;

	/* Now the signature. */
	ret = jwt_sign(b64, buf);
	if (ret)
		goto encode_bio_done;

	len2 = BIO_pending(bmem);
	if (len2 > len) {
		buf.resize(len2);		
	} else if (len2 < 0) {
		ret = EINVAL;
		goto encode_bio_done;
	}

	len2 = BIO_read(bmem, &buf[0], len2);	
	buf.resize(len2);

	base64uri_encode(buf);

	BIO_puts(out, buf.c_str());

encode_bio_success:
	BIO_flush(out);

	ret = 0;

encode_bio_done:
	/* All done. */
	BIO_free_all(b64);

	return ret;
}

void jwt::jwt_write_bio_head(BIO *bio, bool pretty)
{
	BIO_puts(bio, "{");

	if (pretty)
		BIO_puts(bio, "\n");

	/* An unsecured JWT is a JWS and provides no "typ".
	* -- draft-ietf-oauth-json-web-token-32 #6. */
	if (alg != JWT_ALG_NONE) {
		if (pretty)
			BIO_puts(bio, "    ");

		BIO_printf(bio, "\"typ\":%s\"JWT\",", pretty ? " " : "");

		if (pretty)
			BIO_puts(bio, "\n");
	}

	if (pretty)
		BIO_puts(bio, "    ");

	BIO_printf(bio, "\"alg\":%s\"%s\"", pretty ? " " : "",
		jwt_alg_str(alg));

	if (pretty)
		BIO_puts(bio, "\n");

	BIO_puts(bio, "}");

	if (pretty)
		BIO_puts(bio, "\n");

	BIO_flush(bio);
}

void jwt::jwt_write_bio_body(BIO *bio, bool pretty)
{
	// Sort keys for repeatability
	grants.sort();

	std::ostringstream buf;
	boost::property_tree::json_parser::write_json(buf, grants, pretty);
	std::string serial = buf.str();

	BIO_puts(bio, serial.data());
	
	if (pretty)
		BIO_puts(bio, "\n");

	BIO_flush(bio);
}

const char *jwt::jwt_alg_str(jwt_alg_t alg)
{
	switch (alg) {
	case JWT_ALG_NONE:
		return "none";
	case JWT_ALG_HS256:
		return "HS256";
	case JWT_ALG_HS384:
		return "HS384";
	case JWT_ALG_HS512:
		return "HS512";
	case JWT_ALG_RS256:
		return "RS256";
	case JWT_ALG_RS384:
		return "RS384";
	case JWT_ALG_RS512:
		return "RS512";
	case JWT_ALG_ES256:
		return "ES256";
	case JWT_ALG_ES384:
		return "ES384";
	case JWT_ALG_ES512:
		return "ES512";
	default:
		return NULL; // LCOV_EXCL_LINE
	}
}