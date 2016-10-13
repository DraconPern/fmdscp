#include <boost/property_tree/json_parser.hpp>
#include <openssl/bio.h>

typedef enum jwt_alg {
	JWT_ALG_NONE = 0,
	JWT_ALG_HS256,
	JWT_ALG_HS384,
	JWT_ALG_HS512,
	JWT_ALG_RS256,
	JWT_ALG_RS384,
	JWT_ALG_RS512,
	JWT_ALG_ES256,
	JWT_ALG_ES384,
	JWT_ALG_ES512,
	JWT_ALG_TERM
} jwt_alg_t;

class jwt {
public:		
	~jwt();
	int jwt_decode(const std::string &token, const std::string &key);

	std::string jwt_get_grant(const std::string &grant);
	long jwt_get_grant_int(const std::string &grant);
	int jwt_add_grant(const std::string &grant, const std::string &val);
	int jwt_add_grant_int(const std::string &grant, long val);
	int jwt_del_grant(const std::string &grant);

	std::string jwt_encode_str();
	int jwt_set_alg(const jwt_alg_t alg, const std::string &key);
	jwt_alg_t jwt_get_alg();
protected:
	int jwt_verify_head(const std::string &head);
	int jwt_parse_body(const std::string &body);
	int jwt_sign(BIO *out, const std::string &str);
	void base64uri_encode(std::string &str);
	int jwt_sign_sha_hmac(BIO *out, const EVP_MD *alg, const std::string &str);
	int jwt_sign_sha_pem(BIO *out, const EVP_MD *alg, const std::string &str);
	int jwt_str_alg(const std::string &alg);
	boost::property_tree::ptree jwt_b64_decode(std::string src);
	int jwt_encode_bio(BIO *out);
	void jwt_write_bio_head(BIO *bio, bool pretty);
	void jwt_write_bio_body(BIO *bio, bool pretty);
	const char *jwt_alg_str(jwt_alg_t alg);

	boost::property_tree::ptree grants;
	jwt_alg_t alg;
	std::string key;
};