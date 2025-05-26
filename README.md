# CustomFPS Controller 🎮

**By SageInfinity**  
*A sleek FPS controller with real-time adjustments and visual feedback*

![image](https://github.com/user-attachments/assets/f0d69a61-541e-4fe8-8f8d-4553aea3500e)![image](https://github.com/user-attachments/assets/ba2c28b6-0152-4684-980b-9be5bbe64a0c) 

## Features ✨
- Real-time FPS control (1-480 FPS)  
- GPU selection support  
- Custom image background display  
- Neon-styled UI with shadow effects  
- Smooth FPS counter averaging  
- Portable EXE build support  

## Installation ⚙️
-Just download exe file and an image of required resolution. 
-Run the file, input fps and browse the image, select GPU and START.

### Build from Source
1. Clone repository
2. Place these files in root:
   - `NEON_FONT.ttf`  
   - `icon.ico`  
   - `setup_background.png` (optional)
3. Build EXE:
```bash
pyinstaller CustomFPS.spec --noconfirm
```

## Usage 🚀
1. **Setup Screen**  
   - Enter target FPS (1-480)  
   - Select image file (PNG/JPG)  
   - Choose GPU (multi-GPU systems)  
   - Click START

2. **Main Interface**  
   - Drag slider or type FPS value  
   - SPACE: Pause/Resume  
   - ESC: Exit program  


## Customization 🎨
1. **Theme Colors**  
   Modify `PRIMARY_COLOR`, `ACCENT_COLOR` in constants  
2. **Fonts**  
   Replace `NEON_FONT.ttf` (requires TTF file)  
3. **Icons**  
   Update `icon.ico` (multi-resolution recommended)  

## Troubleshooting ⚠️
| Issue | Solution |
|-------|----------|
| Missing font | Ensure `NEON_FONT.ttf` in root folder |
| No icon in EXE | Verify `icon.ico` in build directory |
| GPU not detected | Install latest graphics drivers |
| Image load failed | Use absolute paths for images |

## Credits 🙌
**Developer**: SageInfinity  
**License**: MIT License  
**Special Thanks**: Pygame community

---

**Need Help?**  
Open an issue or contact akp.fb343@gmail.com or on Lossless Scaling discord server.
```


