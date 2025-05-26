import os
import sys
import wmi
import pygame
import tkinter as tk
from tkinter import filedialog
from pygame.locals import *

def resource_path(relative_path):
    """ Get absolute path to resource """
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)

# Constants
FONT_FILE = "NEON_FONT.ttf"
PRIMARY_COLOR = (0, 0, 0)
SECONDARY_COLOR = (20, 20, 30)
ACCENT_COLOR = (0, 255, 255)
TEXT_COLOR = (0, 255, 255)
SHADOW_COLOR = (0, 50, 50)
OUTLINE_ACTIVE = (0, 200, 200)
OUTLINE_INACTIVE = (10, 50, 50)

class FPSController:
    def __init__(self, initial_fps, max_fps=480):
        self.max_fps = max_fps
        self.min_fps = 1
        self.current_fps = initial_fps
        self.paused = False
        self.slider_width = 660
        self.slider_height = 14
        self.thumb_width = 18
        self.thumb_height = 28
        self.input_box = pygame.Rect(0, 0, 70, 32)
        self.active_input = False
        self.input_text = str(initial_fps)
        self.dragging = False
        self.font = pygame.font.Font(resource_path(FONT_FILE), 26)
        self.last_fps_update = 0
        self.display_fps = 0.0
        self.fps_samples = []

    def update_slider_from_fps(self):
        return ((self.current_fps - self.min_fps) / (self.max_fps - self.min_fps)) * self.slider_width

    def handle_events(self, event, surface):
        screen_width, screen_height = surface.get_size()
        input_box_x = (screen_width - self.input_box.w) // 2
        input_box_y = (screen_height // 2) - 40
        self.input_box.topleft = (input_box_x, input_box_y)
        slider_x = (screen_width - self.slider_width) // 2
        slider_y = input_box_y + 60

        if event.type == MOUSEBUTTONDOWN:
            if slider_x <= event.pos[0] <= slider_x + self.slider_width and \
               slider_y <= event.pos[1] <= slider_y + self.slider_height:
                self.dragging = True
            elif self.input_box.collidepoint(event.pos):
                self.active_input = not self.active_input
                pygame.key.start_text_input() if self.active_input else pygame.key.stop_text_input()
            else:
                self.active_input = False
                pygame.key.stop_text_input()

        if event.type == MOUSEBUTTONUP:
            self.dragging = False

        if event.type == MOUSEMOTION and self.dragging:
            x = max(slider_x, min(event.pos[0], slider_x + self.slider_width))
            self.current_fps = int(((x - slider_x)/self.slider_width)*(self.max_fps-self.min_fps)+self.min_fps)
            self.input_text = str(self.current_fps)

        if event.type == KEYDOWN and self.active_input:
            if event.key == K_RETURN:
                self.active_input = False
                pygame.key.stop_text_input()
            elif event.key == K_BACKSPACE:
                self.input_text = self.input_text[:-1]
            elif event.unicode.isdigit():
                self.input_text += event.unicode
                
            if self.input_text:
                try:
                    new_fps = int(self.input_text)
                    self.current_fps = max(self.min_fps, min(new_fps, self.max_fps))
                    self.input_text = str(self.current_fps)
                except ValueError:
                    pass

    def update_fps_counter(self, current_fps):
        now = pygame.time.get_ticks()
        self.fps_samples.append(current_fps)
        
        if now - self.last_fps_update > 500:  # Update every 500ms
            self.display_fps = sum(self.fps_samples) / len(self.fps_samples)
            self.fps_samples = []
            self.last_fps_update = now

    def draw(self, surface, actual_fps):
        self.update_fps_counter(actual_fps)
        screen_width, screen_height = surface.get_size()
        input_box_x = (screen_width - self.input_box.w) // 2
        input_box_y = (screen_height // 2) - 40
        self.input_box.topleft = (input_box_x, input_box_y)
        slider_x = (screen_width - self.slider_width) // 2
        slider_y = input_box_y + 60

        # Slider
        pygame.draw.rect(surface, SECONDARY_COLOR, (slider_x, slider_y, self.slider_width, self.slider_height), border_radius=7)
        pygame.draw.rect(surface, ACCENT_COLOR, (slider_x, slider_y, self.slider_width, self.slider_height), 2, 7)
        
        # Thumb
        thumb_x = slider_x + self.update_slider_from_fps()
        pygame.draw.rect(surface, ACCENT_COLOR, (thumb_x - self.thumb_width//2, slider_y - 7, 
                       self.thumb_width, self.thumb_height), border_radius=5)

        # Input box
        txt_surface = self.font.render(self.input_text, True, TEXT_COLOR)
        shadow_surface = self.font.render(self.input_text, True, SHADOW_COLOR)
        width = max(70, txt_surface.get_width() + 20)
        self.input_box.w = width
        
        pygame.draw.rect(surface, SECONDARY_COLOR, self.input_box, border_radius=5)
        box_color = OUTLINE_ACTIVE if self.active_input else OUTLINE_INACTIVE
        pygame.draw.rect(surface, box_color, self.input_box, 2, 5)
        
        surface.blit(shadow_surface, (self.input_box.x+12, self.input_box.y+6))
        surface.blit(txt_surface, (self.input_box.x+10, self.input_box.y+4))

        # Status text
        status_text = (
            f"PAUSED (SPACE to resume) | Actual: {self.display_fps:.1f}FPS" 
            if self.paused 
            else f"RUNNING Target: {self.current_fps}FPS | Actual: {self.display_fps:.1f}FPS"
        )
        status_surface = self.font.render(status_text, True, TEXT_COLOR)
        shadow_status = self.font.render(status_text, True, SHADOW_COLOR)
        status_rect = status_surface.get_rect(center=(screen_width//2, screen_height - 30))
        surface.blit(shadow_status, status_rect.move(2, 2))
        surface.blit(status_surface, status_rect)

class SetupGUI:
    def __init__(self):
        pygame.init()
        pygame.display.set_caption("CustomFPS - SageInfinity")
        self.screen = pygame.display.set_mode((500, 550))
        self.clock = pygame.time.Clock()
        self.font = pygame.font.Font(resource_path(FONT_FILE), 24)
        
        try:
            icon = pygame.image.load(resource_path('icon.ico'))
            pygame.display.set_icon(icon)
        except Exception as e:
            print(f"Error loading icon: {e}")
        
        try:
            self.background = pygame.image.load(resource_path('setup_background.png')).convert()
            self.background = pygame.transform.scale(self.background, (500, 550))
        except Exception as e:
            self.background = None
        
        base_y = 40
        element_width = 200

        self.inputs = [
            {"rect": pygame.Rect((500-element_width)//2, base_y, element_width, 40), 
             "text": "", "active": False, "label": "Target FPS:"},
            {"rect": pygame.Rect((500-element_width)//2, base_y + 100, element_width, 40),
             "text": "", "active": False, "label": "Image Path:"},
        ]
        
        self.browse_rect = pygame.Rect((500-100)//2, base_y + 170, 100, 40)
        self.gpu_rect = pygame.Rect((500-300)//2, base_y + 250, 300, 40)
        self.selected_gpu = 0
        self.gpus = self.detect_gpus()
        self.start_rect = pygame.Rect((500-120)//2, base_y + 350, 120, 50)

    def detect_gpus(self):
        try:
            wmi_obj = wmi.WMI()
            return [adapter.Name for adapter in wmi_obj.Win32_VideoController()]
        except Exception as e:
            return ["Default GPU"]

    def run(self):
        current_input = None
        start_clicked = False
        
        while not start_clicked:
            if self.background:
                self.screen.blit(self.background, (0, 0))
            else:
                self.screen.fill(PRIMARY_COLOR)
            
            for event in pygame.event.get():
                if event.type == QUIT:
                    pygame.quit()
                    sys.exit()
                
                if event.type == MOUSEBUTTONDOWN:
                    # Input field handling
                    any_input_active = False
                    for inp in self.inputs:
                        if inp["rect"].collidepoint(event.pos):
                            current_input = inp
                            inp["active"] = True
                            any_input_active = True
                        else:
                            inp["active"] = False
                    
                    if not any_input_active:
                        current_input = None
                    
                    # Button handling
                    if self.gpu_rect.collidepoint(event.pos):
                        self.selected_gpu = (self.selected_gpu + 1) % len(self.gpus)
                    elif self.browse_rect.collidepoint(event.pos):
                        root = tk.Tk()
                        root.withdraw()
                        file_path = filedialog.askopenfilename()
                        root.destroy()
                        if file_path:
                            self.inputs[1]["text"] = file_path
                    elif self.start_rect.collidepoint(event.pos):
                        try:
                            target_fps = int(self.inputs[0]["text"])
                            image_path = self.inputs[1]["text"].strip('"')
                            if 1 <= target_fps <= 480 and os.path.exists(image_path):
                                os.environ["SDL_VIDEO_ADAPTER_INDEX"] = str(self.selected_gpu)
                                start_clicked = True
                        except (ValueError, IndexError):
                            pass
                
                if event.type == KEYDOWN and current_input:
                    if event.key == K_RETURN:
                        current_input["active"] = False
                        current_input = None
                    elif event.key == K_BACKSPACE:
                        current_input["text"] = current_input["text"][:-1]
                    else:
                        current_input["text"] += event.unicode

            # Input field rendering
            for inp in self.inputs:
                if inp["label"] == "Image Path:" and inp["text"]:
                    text_width = self.font.size(inp["text"])[0] + 20
                    inp["rect"].w = max(200, text_width)
                    inp["rect"].x = (500 - inp["rect"].w) // 2

                label_surface = self.font.render(inp["label"], True, TEXT_COLOR)
                label_shadow = self.font.render(inp["label"], True, SHADOW_COLOR)
                label_rect = label_surface.get_rect(center=(500//2, inp["rect"].y - 25))
                self.screen.blit(label_shadow, label_rect.move(2, 2))
                self.screen.blit(label_surface, label_rect)
                
                box_color = OUTLINE_ACTIVE if inp["active"] else OUTLINE_INACTIVE
                pygame.draw.rect(self.screen, SECONDARY_COLOR, inp["rect"], border_radius=5)
                pygame.draw.rect(self.screen, box_color, inp["rect"], 2, 5)
                
                if inp["text"]:
                    txt_surface = self.font.render(inp["text"], True, TEXT_COLOR)
                    txt_shadow = self.font.render(inp["text"], True, SHADOW_COLOR)
                    self.screen.blit(txt_shadow, (inp["rect"].x + 12, inp["rect"].y + 6))
                    self.screen.blit(txt_surface, (inp["rect"].x + 10, inp["rect"].y + 4))

            # Button rendering
            buttons = [
                (self.browse_rect, "Browse"),
                (self.gpu_rect, self.gpus[self.selected_gpu]),
                (self.start_rect, "START")
            ]
            
            for rect, text in buttons:
                pygame.draw.rect(self.screen, SECONDARY_COLOR, rect, border_radius=5)
                pygame.draw.rect(self.screen, OUTLINE_INACTIVE, rect, 2, 5)
                
                text_surface = self.font.render(text, True, TEXT_COLOR)
                text_shadow = self.font.render(text, True, SHADOW_COLOR)
                text_rect = text_surface.get_rect(center=rect.center)
                self.screen.blit(text_shadow, text_rect.move(2, 2))
                self.screen.blit(text_surface, text_rect)

            pygame.display.flip()
            self.clock.tick(30)

        return int(self.inputs[0]["text"]), self.inputs[1]["text"].strip('"')

def main():
    setup = SetupGUI()
    target_fps, image_path = setup.run()
    
    try:
        image = pygame.image.load(image_path)
    except pygame.error as e:
        print(f"Error loading image: {e}")
        pygame.quit()
        return

    screen = pygame.display.set_mode(image.get_size(), RESIZABLE)
    pygame.display.set_caption("CustomFPS - SageInfinity")
    
    try:
        icon = pygame.image.load(resource_path('icon.ico'))
        pygame.display.set_icon(icon)
    except Exception as e:
        print(f"Error loading icon: {e}")

    clock = pygame.time.Clock()
    fps_controller = FPSController(target_fps)
    
    running = True
    fps_history = []
    
    while running:
        for event in pygame.event.get():
            if event.type == QUIT:
                running = False
            if event.type == KEYDOWN:
                if event.key == K_ESCAPE:
                    running = False
                elif event.key == K_SPACE:
                    fps_controller.paused = not fps_controller.paused
            fps_controller.handle_events(event, screen)

        screen.blit(image, (0, 0))
        
        delta_time = clock.tick(fps_controller.current_fps if not fps_controller.paused else 30)
        current_fps = 1000.0 / delta_time if delta_time > 0 else 0.0
        fps_history.append(current_fps)
        
        fps_controller.draw(screen, current_fps)
        pygame.display.flip()

    pygame.quit()

if __name__ == "__main__":
    main()