from typing import Optional
import ipaddress
import re
import shutil
import subprocess
import serial.tools.list_ports
from pydantic import BaseModel, ValidationError, field_validator
from pathlib import Path
import typer
import questionary
import os


CONFIG_EXAMPLE = Path("data/config.example.json")
CONFIG_SAVED = Path("data/default-config.json")
CONFIG_OUT = Path("data/config.json")
# A projekt útvonala nem ASCII (Takács Dávid), ezért a pio-nak ASCII build mappa kell (README 6. pont).
BUILD_DIR = "C:/pio_build"
# Build módok = a platformio.ini env nevei. A native tesztkörnyezet szándékosan nincs itt.
BUILD_MODES = {
    "dev": "dev - soros napló bekapcsolva",
    "prod": "prod - soros napló kikapcsolva",
}
DEFAULT_BUILD_MODE = "dev"
VERSION = "0.1.0"


class SerialDeviceModel(BaseModel):
    id:int
    name:str
    comname:str


def check_ip(value: str) -> str:
    ipaddress.ip_address(value)
    return value


def check_not_empty(value: str) -> str:
    if not value.strip():
        raise ValueError("Nem lehet üres")
    return value.strip()


def check_device_name(value: str) -> str:
    # A firmware szabálya (Config.cpp): 1..64 karakter, vezérlőkarakter nélkül.
    value = value.strip()
    if not 1 <= len(value) <= 64 or re.search(r"[\x00-\x1f\x7f]", value):
        raise ValueError("1..64 karakter lehet, vezérlőkarakter nélkül")
    return value


def check_hostname(value: str) -> str:
    # A firmware szabálya (Config.cpp): 1..32 karakter, betű/szám/kötőjel, nem kezdődik és nem végződik kötőjellel.
    if not re.fullmatch(r"[A-Za-z0-9]([A-Za-z0-9-]{0,30}[A-Za-z0-9])?", value):
        raise ValueError("1..32 karakter, csak betű, szám és kötőjel, nem kezdődhet és végződhet kötőjellel")
    return value


class DeviceModel(BaseModel):
    name:str
    ip:str
    hostname:str
    token:str

    _check_name = field_validator("name")(check_device_name)
    _check_token = field_validator("token")(check_not_empty)
    _check_ip = field_validator("ip")(check_ip)
    _check_hostname = field_validator("hostname")(check_hostname)


class NetModel(BaseModel):
    ip:str
    mask:str
    gateway:str
    dns:str

    _check_ip = field_validator("ip", "mask", "gateway", "dns")(check_ip)

class WifiModel(BaseModel):
    ssid:str
    password:str

class EndpointModel(BaseModel):
    host:str
    port:int
    path:str
    token:str

    _check_host = field_validator("host")(check_ip)

class ConfigModel(BaseModel):
    wifi: WifiModel
    net: NetModel
    endpoint:EndpointModel
    i2s: dict[str,int]
    buffer:dict[str,int]

class DeviceInfoModel(BaseModel):
    name:str
    hostname:str

class BuildConfigModel(ConfigModel):
    device: DeviceInfoModel

class MenuModel(BaseModel):
    question:str = "Mit szeretnél?"
    choice:Optional[list[str]] = None
    input:bool = False




class Serial():
    def __init__(self):
        self.devices:list[SerialDeviceModel] = []
    def __call__(self):
        for i in self.devices:
            print(i.name)

    def get_serials(self):
        for i,p in enumerate(serial.tools.list_ports.comports(),start=1):
            self.devices.append(SerialDeviceModel(id=i,comname=p.device,name=p.description))



def clear():
    os.system("cls" if os.name == "nt" else "clear")

def error_text(error: ValidationError) -> str:
    return error.errors()[0]["msg"]

def prompt_check(check):
    def validate(text: str):
        try:
            check(text)
        except ValueError as e:
            return str(e)
        return True
    return validate

class DefaultConfig():
    SECRETS = {"password"}
    # A token nem lehet az alapconfig része: sem szerkeszteni, sem menteni nem lehet itt.
    LOCKED = {("endpoint", "token")}

    def __init__(self) -> None:
        self.menu = MenuModel(choice=["Megnézés","Módosítás","Mentés","Vissza"])
        self.config = self.load()

    def load(self) -> ConfigModel:
        self.saved = False
        self.dirty = False
        example = ConfigModel.model_validate_json(CONFIG_EXAMPLE.read_text(encoding="utf-8"))
        self.placeholder_token = example.endpoint.token
        if CONFIG_SAVED.exists():
            try:
                config = ConfigModel.model_validate_json(CONFIG_SAVED.read_text(encoding="utf-8"))
                self.saved = True
                return self.without_token(config)
            except ValidationError as e:
                print(f"A mentett config hibás ({error_text(e)}), a példa configot töltöm be.")
                input("\nEnter a folytatáshoz...")
        return example

    def without_token(self, config: ConfigModel) -> ConfigModel:
        return config.model_copy(update={"endpoint": config.endpoint.model_copy(update={"token": self.placeholder_token})})

    def status(self) -> str:
        if self.dirty:
            return "Módosítva, még nincs mentve"
        if self.saved:
            return "Mentve"
        return "Nincs elmentve (a példa config alapján)"

    def show(self) -> None:
        clear()
        print(f"Állapot: {self.status()}\n")
        for section, values in self.config.model_dump().items():
            print(f"[{section}]")
            for key, value in values.items():
                if (section, key) in self.LOCKED:
                    value = "(nem része az alapconfignak)"
                elif key in self.SECRETS:
                    value = "*" * len(str(value))
                print(f"  {key}: {value}")
        input("\nEnter a folytatáshoz...")

    def with_value(self, section: str, key: str, text: str) -> ConfigModel:
        data = self.config.model_dump()
        data[section][key] = text
        return ConfigModel.model_validate(data)

    def edit(self) -> None:
        while True:
            clear()
            print(f"Állapot: {self.status()}\n")
            data = self.config.model_dump()
            section = questionary.select("Melyik szekció?", choices=[*data, "Vissza"]).ask()
            if section in (None, "Vissza"):
                return
            fields = [k for k in data[section] if (section, k) not in self.LOCKED]
            key = questionary.select(f"[{section}] Melyik mező?", choices=[*fields, "Vissza"]).ask()
            if key in (None, "Vissza"):
                continue

            def check(text: str):
                try:
                    self.with_value(section, key, text)
                except ValidationError as e:
                    return error_text(e)
                return True

            text = questionary.text(f"{section}.{key}:", default=str(data[section][key]), validate=check).ask()
            if text is None:
                continue
            self.config = self.with_value(section, key, text)
            self.dirty = True

    def save(self) -> None:
        CONFIG_SAVED.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_SAVED.write_text(self.without_token(self.config).model_dump_json(indent=2), encoding="utf-8")
        self.saved = True
        self.dirty = False
        print(f"Elmentve: {CONFIG_SAVED}")
        input("\nEnter a folytatáshoz...")

    def __call__(self) -> None:
        while True:
            clear()
            print(f"Állapot: {self.status()}\n")
            choice = questionary.select(self.menu.question, choices=self.menu.choice).ask()

            if choice == "Megnézés":
                self.show()
            elif choice == "Módosítás":
                self.edit()
            elif choice == "Mentés":
                self.save()
            elif choice in (None, "Vissza"):
                if self.dirty and not questionary.confirm("Van el nem mentett módosítás. Eldobod?", default=False).ask():
                    continue
                if self.dirty:
                    self.config = self.load()
                break

class Configure():
    def __init__(self, defaultconfig: DefaultConfig) -> None:
        self.defaultconfig = defaultconfig
        self.device: Optional[DeviceModel] = None

    def __call__(self) -> None:
        clear()
        print(f"Szoftver verzió: {VERSION}\n")
        name = questionary.text("Eszköz neve:", default=self.device.name if self.device else "",
                                validate=prompt_check(check_device_name)).ask()
        if name is None:
            return
        ip = questionary.text("IP cím:", default=self.device.ip if self.device else self.defaultconfig.config.net.ip,
                              validate=prompt_check(check_ip)).ask()
        if ip is None:
            return
        hostname = questionary.text("Hostname:", default=self.device.hostname if self.device else "",
                                    validate=prompt_check(check_hostname)).ask()
        if hostname is None:
            return
        token = questionary.password("Token:", validate=prompt_check(check_not_empty)).ask()
        if token is None:
            return
        self.device = DeviceModel(name=name, ip=ip, hostname=hostname, token=token)
        print("\nAz adatok a memóriában vannak, a token nem kerül fájlba.")
        input("\nEnter a folytatáshoz...")

class Build():
    def __init__(self, defaultconfig: DefaultConfig, configure: Configure) -> None:
        self.defaultconfig = defaultconfig
        self.configure = configure
        self.port: Optional[str] = None
        self.mode = DEFAULT_BUILD_MODE
        self.menu = MenuModel(choice=["config.json létrehozása","Eszköz kiválasztása","Build mód (dev/prod)","Fordítás és feltöltés","Soros monitor","Vissza"])

    def make_config(self) -> bool:
        device = self.configure.device
        if device is None:
            print("Előbb töltsd ki a Configurálás menüt.")
            return False
        data = self.defaultconfig.config.model_dump()
        data["net"]["ip"] = device.ip
        data["endpoint"]["token"] = device.token
        data["device"] = {"name": device.name, "hostname": device.hostname}
        try:
            config = BuildConfigModel.model_validate(data)
        except ValidationError as e:
            print(f"Hibás config: {error_text(e)}")
            return False
        CONFIG_OUT.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_OUT.write_text(config.model_dump_json(indent=2), encoding="utf-8")
        print(f"Elkészült: {CONFIG_OUT}")
        return True

    def choose_port(self) -> None:
        ports = Serial()
        ports.get_serials()
        if not ports.devices:
            print("Nincs csatlakoztatott soros eszköz.")
            return
        labels = {f"{d.comname} - {d.name}": d.comname for d in ports.devices}
        choice = questionary.select("Melyik eszköz?", choices=[*labels, "Vissza"]).ask()
        if choice in labels:
            self.port = labels[choice]

    def choose_mode(self) -> None:
        labels = {label: mode for mode, label in BUILD_MODES.items()}
        choice = questionary.select("Melyik build?", choices=[*labels, "Vissza"],
                                    default=BUILD_MODES[self.mode]).ask()
        if choice in labels:
            self.mode = labels[choice]
            print(f"Build mód: {BUILD_MODES[self.mode]}")

    def pio(self, *args: str) -> int:
        exe = shutil.which("pio") or shutil.which("platformio")
        if exe is None:
            print("A pio nem található a PATH-on.")
            return 1
        # A uv-s venv launcher PYTHONHOME-ot állít be; a pio saját (más verziójú) Pythonja ettől a hibás stdlibet töltené be.
        env = {k: v for k, v in os.environ.items() if k not in ("PYTHONHOME", "UV_INTERNAL__PYTHONHOME")}
        env["PLATFORMIO_BUILD_DIR"] = BUILD_DIR
        try:
            return subprocess.run([exe, *args], env=env).returncode
        except KeyboardInterrupt:
            return 130

    def upload(self) -> None:
        if self.port is None:
            print("Előbb válassz eszközt.")
            return
        if not self.make_config():
            return
        print(f"Build mód: {BUILD_MODES[self.mode]}\n")
        for target in ("uploadfs", "upload"):
            if self.pio("run", "-e", self.mode, "-t", target, "--upload-port", self.port) != 0:
                print(f"A(z) {target} hibával leállt.")
                return
        print(f"Kész ({self.mode}).")

    def monitor(self) -> None:
        if self.port is None:
            print("Előbb válassz eszközt.")
            return
        if self.mode == "prod":
            print("Figyelem: a prod firmware nem ír soros naplót, csak a bootloader üzenetei látszanak.\n")
        self.pio("device", "monitor", "-e", self.mode, "--port", self.port)

    def __call__(self) -> None:
        while True:
            clear()
            print(f"Eszköz: {self.port or 'nincs kiválasztva'}")
            print(f"Build mód: {BUILD_MODES[self.mode]}\n")
            choice = questionary.select(self.menu.question, choices=self.menu.choice).ask()
            if choice in (None, "Vissza"):
                break
            clear()
            if choice == "config.json létrehozása":
                self.make_config()
            elif choice == "Eszköz kiválasztása":
                self.choose_port()
            elif choice == "Build mód (dev/prod)":
                self.choose_mode()
            elif choice == "Fordítás és feltöltés":
                self.upload()
            elif choice == "Soros monitor":
                self.monitor()
            input("\nEnter a folytatáshoz...")

class Main():
    def __init__(self) -> None:
        self.app = typer.Typer(help="Arduino setup")
        self.mainmenu =  MenuModel(choice=["Alapconfig","Configurálás","Build","Kilépés"])
        self.defaultconfig = DefaultConfig()
        self.configure = Configure(self.defaultconfig)
        self.build = Build(self.defaultconfig, self.configure)
    def __call__(self):
        while True:
            clear()
            choice = questionary.select(self.mainmenu.question,choices=self.mainmenu.choice).ask()

            if choice == "Alapconfig":
                self.defaultconfig()
            elif choice == "Configurálás":
                self.configure()
            elif choice == "Build":
                self.build()
            elif choice == "Kilépés":
                break
            else:
                pass


if __name__ == "__main__":
    clear()
    Main()()
