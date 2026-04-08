"""
Lumina Engine Setup Script
Automates dependency download, extraction, and project configuration
"""

import os
import sys
import time
import shutil
import subprocess
from pathlib import Path

# Ensure required packages are installed
def ensure_package(package_name, import_name=None):
    """Install package if not available."""
    import_name = import_name or package_name
    try:
        __import__(import_name)
    except ImportError:
        subprocess.check_call([sys.executable, "-m", "pip", "install", package_name], 
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

ensure_package("colorama")
ensure_package("requests")
ensure_package("py7zr")

from colorama import Fore, Style, init
import requests
import py7zr
from py7zr.callbacks import ExtractCallback

init(autoreset=True)


class SetupDisplay:
    
    WIDTH = 80
    
    @staticmethod
    def header(text):
        print(f"\n{Fore.CYAN}{Style.BRIGHT}{'=' * SetupDisplay.WIDTH}")
        print(f"{Fore.CYAN}{Style.BRIGHT}{text.center(SetupDisplay.WIDTH)}")
        print(f"{Fore.CYAN}{Style.BRIGHT}{'=' * SetupDisplay.WIDTH}\n")
    
    @staticmethod
    def step(num, total, description):
        step_text = f"Step {num}/{total}: {description}"
        print(f"\n{Fore.MAGENTA}{Style.BRIGHT}{step_text}")
        print(f"{Fore.MAGENTA}{'-' * len(step_text)}\n")
    
    @staticmethod
    def info(message):
        print(f"{Fore.CYAN}{message}")
    
    @staticmethod
    def success(message):
        print(f"{Fore.GREEN}{Style.BRIGHT}{message}")
    
    @staticmethod
    def warning(message):
        print(f"{Fore.YELLOW}{message}")
    
    @staticmethod
    def error(message):
        print(f"{Fore.RED}{Style.BRIGHT}{message}")
    
    @staticmethod
    def progress_bar(current, total, prefix="Progress", bar_length=50):
        if total == 0:
            return
        
        current = min(current, total)
        filled = int(bar_length * current / total)
        bar = '|' * filled + '_' * (bar_length - filled)
        percent = f"{100 * current / total:.1f}%".rjust(6)
        
        sys.stdout.write(f'\r{Fore.YELLOW}{prefix}: [{Fore.GREEN}{bar}{Fore.YELLOW}] {percent}')
        sys.stdout.flush()
        
        if current >= total:
            print()


class DependencyDownloader:
    
    def __init__(self, url, destination):
        self.url = self._normalize_url(url)
        self.destination = Path(destination)
        self.chunk_size = 8192
    
    @staticmethod
    def _normalize_url(url):
        """Ensure Dropbox URL triggers direct download."""
        if "dl=0" in url:
            return url.replace("dl=0", "dl=1")
        elif "dl=1" not in url:
            return f"{url}?dl=1"
        return url
    
    def download(self):

        SetupDisplay.info("Connecting to Dropbox...")
        
        try:
            response = requests.get(self.url, stream=True, timeout=30)
            response.raise_for_status()
            
            total_size = int(response.headers.get('content-length', 0))
            downloaded = 0
            
            SetupDisplay.success("Connection established")
            SetupDisplay.info(f"File size: {total_size / (1024**2):.2f} MB\n")
            
            start_time = time.time()
            last_update = start_time
            
            with open(self.destination, 'wb') as f:
                for chunk in response.iter_content(chunk_size=self.chunk_size):
                    if chunk:
                        f.write(chunk)
                        downloaded += len(chunk)
                        
                        current_time = time.time()
                        if current_time - last_update >= 0.1 or downloaded >= total_size:
                            elapsed = current_time - start_time
                            if elapsed > 0 and total_size > 0:
                                speed = downloaded / elapsed / (1024**2)
                                prefix = f"Downloading ({speed:.2f} MB/s)"
                                SetupDisplay.progress_bar(downloaded, total_size, prefix)
                            last_update = current_time
            
            print()
            SetupDisplay.success(f"Download complete: {self.destination.name}\n")
            return True
            
        except requests.RequestException as e:
            SetupDisplay.error(f"Download failed: {e}")
            return False
        except Exception as e:
            SetupDisplay.error(f"Unexpected error: {e}")
            return False


class ArchiveExtractor:
    
    def __init__(self, archive_path, extract_to):
        self.archive_path = Path(archive_path)
        self.extract_to = Path(extract_to)
    
    class ProgressCallback(ExtractCallback):

        def __init__(self, total_files):
            self.total_files = total_files
            self.extracted = 0
            self.last_update = time.time()

        def report_end(self, processing_file_path, wrote_bytes):
            return super().report_end(processing_file_path, wrote_bytes)
        
        def report_start_preparation(self):
            return super().report_start_preparation()
        
        def report_warning(self, message):
            return super().report_warning(message)
        
        def report_start(self, processing_file_path, processing_bytes):
            self.extracted += 1
        
        def report_update(self, decompressed_bytes):
            current_time = time.time()
            if current_time - self.last_update >= 0.05 or self.extracted >= self.total_files:
                SetupDisplay.progress_bar(
                    self.extracted, 
                    self.total_files, 
                    f"Extracting ({self.extracted}/{self.total_files})"
                )
                self.last_update = current_time
            return super().report_update(decompressed_bytes)
        
        def report_postprocess(self):
            SetupDisplay.progress_bar(self.total_files, self.total_files, "Extracting")
    
    def extract(self):
        if not self.archive_path.exists():
            SetupDisplay.error(f"Archive not found: {self.archive_path}")
            return False
        
        SetupDisplay.info("Opening archive...")
        
        try:
            with py7zr.SevenZipFile(self.archive_path, mode='r') as archive:
                file_list = archive.getnames()
                total_files = len(file_list)
                
                SetupDisplay.success("Archive opened successfully")
                SetupDisplay.info(f"Files to extract: {total_files}\n")
                
                callback = self.ProgressCallback(total_files)
                archive.extractall(path=str(self.extract_to), callback=callback)
                
                print()
                SetupDisplay.success(f"Extraction complete: {total_files} files\n")
                return True
                
        except Exception as e:
            SetupDisplay.error(f"Extraction failed: {e}")
            return False


class ProjectGenerator:
    """Handles project file generation."""
    
    def __init__(self, script_path):
        self.script_path = Path(script_path)
    
    def generate(self):
        """Run project generation script."""
        if not self.script_path.exists():
            SetupDisplay.error(f"Generation script not found: {self.script_path}")
            return False
        
        SetupDisplay.info("Generating project files...\n")
        
        try:
            result = subprocess.run(
                [sys.executable, str(self.script_path)],
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                check=True
            )
            
            if result.stdout:
                print(result.stdout)
            
            SetupDisplay.success("Project generation complete\n")
            return True
            
        except subprocess.CalledProcessError as e:
            SetupDisplay.error("Project generation failed")
            if e.stderr:
                print(e.stderr)
            return False


def cleanup_file(filepath):
    """Remove a file if it exists."""
    path = Path(filepath)
    if path.exists():
        try:
            path.unlink()
            SetupDisplay.success(f"Cleaned up: {path.name}")
        except Exception as e:
            SetupDisplay.warning(f"Could not remove {path.name}: {e}")


def main():
    try:
        SetupDisplay.header("LUMINA ENGINE SETUP")
        
        print(f"{Fore.WHITE}Welcome to the Lumina Engine setup utility.")
        print(f"{Fore.WHITE}This will download dependencies and configure your project.\n")
        
        dropbox_url = "https://www.dropbox.com/scl/fi/lv66zolocddgylco2a277/External.7z?rlkey=q0iyx20qcgoz7w97v9iawtrz9&st=l4dxbld4&dl=0"
        archive_file = "External.7z"
        extract_location = "."
        generation_script = Path("BuildScripts") / "GenerateProjectFiles.py"
        
        total_steps = 4
        
        SetupDisplay.step(1, total_steps, "Downloading Dependencies")
        downloader = DependencyDownloader(dropbox_url, archive_file)
        if not downloader.download():
            sys.exit(1)
        
        SetupDisplay.step(2, total_steps, "Extracting Dependencies")
        extractor = ArchiveExtractor(archive_file, extract_location)
        if not extractor.extract():
            sys.exit(1)
        
        SetupDisplay.step(3, total_steps, "Generating Project Files")
        generator = ProjectGenerator(generation_script)
        if not generator.generate():
            sys.exit(1)
        
        SetupDisplay.step(4, total_steps, "Cleaning Up")
        cleanup_file(archive_file)


        subprocess.run(
        ["git", "config", "core.hooksPath", "BuildScripts/Hooks"],
        check=True
        )
        print("Git hooks configured.")
        
        print()
        SetupDisplay.header("SETUP COMPLETE")
        SetupDisplay.success("Lumina Engine is ready to use!")
        print(f"\n{Fore.CYAN}You can now open {Style.BRIGHT}Lumina.sln{Style.NORMAL} in Visual Studio.")
        print(f"{Fore.YELLOW}Happy coding!\n")
        print("This window will close in 2 seconds.")
        time.sleep(2.0)
        
    except KeyboardInterrupt:
        print(f"\n\n{Fore.YELLOW}Setup interrupted by user")
        sys.exit(1)
    except Exception as e:
        SetupDisplay.error(f"\nUnexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()