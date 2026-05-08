# Contributing to K230D YOLOv8n Pipeline

Thank you for your interest in contributing! This project aims to provide an accessible, end-to-end pipeline for deploying YOLOv8n models on the K230D Zero board.

## Areas for Contribution

### 1. Documentation
- Improve setup instructions
- Add more troubleshooting tips
- Create video tutorials
- Translate documentation

### 2. Model Optimization
- Experiment with quantization methods
- Optimize post-processing for K230D
- Implement model pruning techniques
- Test different input resolutions

### 3. Performance Improvements
- Optimize CanMV inference script
- Reduce memory usage
- Improve FPS on K230D
- Better NMS implementation

### 4. Testing & Validation
- Add automated tests
- Benchmark different models
- Test on various datasets
- Profile memory usage

### 5. Features
- Support for YOLOv8s, YOLOv8m variants
- Multi-class tracking
- Video processing support
- Remote monitoring dashboard

## Development Setup

```bash
# Fork the repository
git clone https://github.com/yourusername/k230dev-yolo
cd k230dev-yolo

# Create feature branch
git checkout -b feature/your-feature-name

# Make your changes
# ...

# Test your changes
make test

# Commit and push
git add .
git commit -m "Add feature: description"
git push origin feature/your-feature-name

# Create pull request on GitHub
```

## Code Style

### Python
- Follow PEP 8
- Use type hints where applicable
- Add docstrings to functions
- Keep functions focused and small

### Docker
- Keep Dockerfiles clean and commented
- Minimize layer count
- Use multi-stage builds when beneficial

### Documentation
- Use clear, concise language
- Provide examples
- Update README when adding features

## Testing Guidelines

Before submitting:

1. **Test Training:**
   ```bash
   make build-train
   docker compose run --rm train python train_yolov8n.py --epochs 1
   ```

2. **Test Conversion:**
   ```bash
   make build-convert
   # Test with sample model
   ```

3. **Validate Scripts:**
   ```bash
   python -m py_compile workspace/*.py
   ```

4. **Check Documentation:**
   - Ensure all links work
   - Verify code examples are accurate

## Submitting Issues

When reporting bugs, include:

- System information (OS, Docker version, GPU)
- Exact commands that caused the issue
- Full error messages
- Expected vs actual behavior

## Submitting Pull Requests

1. **Small, focused PRs** are better than large ones
2. **Update documentation** if changing functionality
3. **Add tests** if applicable
4. **Reference issues** in PR description
5. **Be responsive** to review feedback

## Code of Conduct

- Be respectful and constructive
- Welcome newcomers
- Focus on the code, not the person
- Assume good intentions

## Questions?

- Open a discussion on GitHub
- Check existing issues
- Review documentation

## License

By contributing, you agree that your contributions will be licensed under the same license as the project.

---

**Thank you for helping make K230D development more accessible!** 🎉
