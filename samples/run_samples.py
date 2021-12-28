import contextlib
import glob
import http.client
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

SECONDS_TO_START = 5.0
SERVICE_HOST = 'localhost'

CONFIGS_PORT = 8083

THIS_DIR = os.path.dirname(os.path.realpath(__file__))


def copy_service_configs(new_dir: str, service_name: str) -> None:
    config_files_list = glob.glob(os.path.join(THIS_DIR, service_name, '*'))
    for config_file_path in config_files_list:
        with open(config_file_path) as conf_file:
            conf = conf_file.read()

        conf = conf.replace('/etc/' + service_name, new_dir)
        conf = conf.replace('/var/cache/' + service_name, new_dir)
        conf = conf.replace('/var/log/' + service_name, new_dir)
        conf = conf.replace('/var/run/' + service_name, new_dir)

        new_path = os.path.join(new_dir, os.path.basename(config_file_path))
        with open(new_path, 'w') as new_conf_file:
            new_conf_file.write(conf)


@contextlib.contextmanager
def start_service(service_name: str, port, timeout=SECONDS_TO_START):
    with tempfile.TemporaryDirectory() as tmpdirname:
        print('### Copying "' + service_name + '" configs into: ' + tmpdirname)
        copy_service_configs(tmpdirname, service_name)
        service = subprocess.Popen(
            [
                os.path.join(service_name, 'userver-samples-' + service_name),
                '--config',
                os.path.join(tmpdirname, 'static_config.yaml'),
            ],
        )

        start_time = time.perf_counter()
        while True:
            if time.perf_counter() - start_time >= timeout:
                service.terminate()
                raise TimeoutError(
                    (
                        'Waited too long for the port {port} on host '
                        '{SERVICE_HOST} to start accepting connections.'
                    ).format(port=port, SERVICE_HOST=SERVICE_HOST),
                )

            try:
                with socket.create_connection(
                        (SERVICE_HOST, port), timeout=timeout,
                ):
                    break
            except OSError:
                time.sleep(0.1)

        yield service

        service.terminate()


def test_hello():
    port = 8080
    with start_service('hello_service', port=port):
        conn = http.client.HTTPConnection(SERVICE_HOST, port=port)
        conn.request('GET', '/hello')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert resp.read() == b'Hello world!\n'


def test_config():
    with start_service('config_service', CONFIGS_PORT):
        conn = http.client.HTTPConnection(SERVICE_HOST, CONFIGS_PORT)
        conn.request('POST', '/configs/values', body='{}')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert b'"USERVER_LOG_REQUEST_HEADERS":true' in resp.read()


def test_flatbuf():
    port = 8084
    with start_service('flatbuf_service', port=port):
        conn = http.client.HTTPConnection(SERVICE_HOST, port)
        body = bytearray.fromhex(
            '100000000c00180000000800100004000c000000140000001400000000000000'
            '16000000000000000a00000048656c6c6f20776f72640000',
        )
        conn.request('POST', '/fbs', body=body)
        with conn.getresponse() as resp:
            assert resp.status == 200


def test_production_service():
    with start_service('config_service', CONFIGS_PORT):
        port = 8085
        with start_service('production_service', port=port):
            conn = http.client.HTTPConnection(SERVICE_HOST, port)
            conn.request('GET', '/service/log-level/')
            resp = conn.getresponse()
            assert resp.status == 200


def test_postgres():
    port = 8086
    with start_service('postgres_service', port=port):
        conn = http.client.HTTPConnection(SERVICE_HOST, port)

        conn.request('DELETE', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200

        conn.request('POST', '/v1/key-value?key=hello&value=world')
        with conn.getresponse() as resp:
            assert resp.status == 201
            assert resp.read() == b'world'

        conn.request('GET', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert resp.read() == b'world'

        conn.request('DELETE', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200

        conn.request('POST', '/v1/key-value?key=hello&value=there')
        with conn.getresponse() as resp:
            assert resp.status == 201
            assert resp.read() == b'there'

        conn.request('GET', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert resp.read() == b'there'

        conn.request('POST', '/v1/key-value?key=hello&value=again')
        with conn.getresponse() as resp:
            assert resp.status == 409  # Conflict
            assert resp.read() == b'there'

        conn.request('GET', '/v1/key-value?key=missing')
        with conn.getresponse() as resp:
            assert resp.status == 404  # Not Found


def test_redis():
    port = 8088
    os.environ[
        'SECDIST_CONFIG'
    ] = """{
        "redis_settings": {
            "taxi-tmp": {
                "password": "",
                "sentinels": [
                    {"host": "localhost", "port": 26379}
                ],
                "shards": [
                    {"name": "test_master0"}
                ]
            }
        }
    }"""
    with start_service('redis_service', port=port):
        conn = http.client.HTTPConnection(SERVICE_HOST, port)

        conn.request('DELETE', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200

        conn.request('POST', '/v1/key-value?key=hello&value=world')
        with conn.getresponse() as resp:
            assert resp.status == 201
            assert resp.read() == b'world'

        conn.request('GET', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert resp.read() == b'world'

        conn.request('POST', '/v1/key-value?key=hello&value=there')
        with conn.getresponse() as resp:
            assert resp.status == 409  # Conflict

        conn.request('GET', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert resp.read() == b'world'  # Still the same

        conn.request('DELETE', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 200
            assert resp.read() == b'1'

        conn.request('GET', '/v1/key-value?key=hello')
        with conn.getresponse() as resp:
            assert resp.status == 404  # Not Found


def test_http_cache():
    port = 8089
    with start_service('mongo_service', port=port + 1):
        with start_service('http_caching', port=port):
            conn = http.client.HTTPConnection(SERVICE_HOST, port)

            username = (
                '%D0%B4%D0%BE%D1%80%D0%BE%D0%B3%D0%BE%D0%B9%20%D1%80%D0%B0'
                '%D0%B7%D1%80%D0%B0%D0%B1%D0%BE%D1%82%D1%87%D0%B8%D0%BA'
            )
            conn.request('POST', '/samples/greet?username=' + username)
            with conn.getresponse() as resp:
                assert resp.status == 200
                data = resp.read().decode('utf-8')
                assert (
                    data == 'Привет, дорогой разработчик! Добро пожаловать'
                ), ('Data is: ' + data)

            update_cache = """
                {"invalidate_caches": {
                    "update_type": "incremental",
                    "names": ["cache-http-translations"]
                }}
            """
            conn.request('POST', '/tests/control', body=update_cache)
            with conn.getresponse() as resp:
                assert resp.status == 200, resp.read().decode('utf-8')

            conn.request('POST', '/samples/greet?username=' + username)
            with conn.getresponse() as resp:
                assert resp.status == 200
                data = resp.read().decode('utf-8')
                assert (
                    data == 'Приветище, дорогой разработчик! Добро пожаловать'
                ), ('Data is: ' + data)


if __name__ == '__main__':
    if '--only-prepare-production-configs' in sys.argv:
        PRODUCTION_SERVICE_CFG_PATH = '/tmp/userver/production_service'

        shutil.rmtree(PRODUCTION_SERVICE_CFG_PATH, ignore_errors=True)
        os.makedirs(PRODUCTION_SERVICE_CFG_PATH)
        copy_service_configs(PRODUCTION_SERVICE_CFG_PATH, 'production_service')
        sys.exit(0)

    test_hello()
    test_config()
    test_flatbuf()
    test_production_service()
    test_postgres()
    test_redis()
    test_http_cache()
