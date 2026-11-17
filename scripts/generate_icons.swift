import Cocoa
import CoreGraphics

func drawCMEUF5Icon(size: CGFloat) -> NSImage {
    let img = NSImage(size: NSSize(width: size, height: size))
    img.lockFocus()
    guard let ctx = NSGraphicsContext.current?.cgContext else {
        img.unlockFocus()
        return img
    }

    let rect = CGRect(x: 0, y: 0, width: size, height: size)
    let pad = size * 0.06
    let cardRect = rect.insetBy(dx: pad, dy: pad)
    let corner = size * 0.18

    // Rounded background shadow & dark metallic body
    let bgPath = CGPath(roundedRect: cardRect, cornerWidth: corner, cornerHeight: corner, transform: nil)
    ctx.saveGState()
    ctx.addPath(bgPath)
    ctx.clip()

    let colorSpace = CGColorSpaceCreateDeviceRGB()
    let bgColors = [
        CGColor(red: 0.16, green: 0.17, blue: 0.20, alpha: 1.0),
        CGColor(red: 0.08, green: 0.09, blue: 0.11, alpha: 1.0)
    ] as CFArray
    if let grad = CGGradient(colorsSpace: colorSpace, colors: bgColors, locations: [0.0, 1.0]) {
        ctx.drawLinearGradient(grad, start: CGPoint(x: 0, y: size), end: CGPoint(x: 0, y: 0), options: [])
    }

    // Iconic CME UF red metallic side cheeks on left and right
    let cheekWidth = cardRect.width * 0.075
    let leftCheek = CGRect(x: cardRect.minX, y: cardRect.minY, width: cheekWidth, height: cardRect.height)
    let rightCheek = CGRect(x: cardRect.maxX - cheekWidth, y: cardRect.minY, width: cheekWidth, height: cardRect.height)
    ctx.setFillColor(CGColor(red: 0.78, green: 0.12, blue: 0.16, alpha: 1.0))
    ctx.fill(leftCheek)
    ctx.fill(rightCheek)

    // Top control surface area
    let innerX = cardRect.minX + cheekWidth + size * 0.03
    let innerW = cardRect.width - (cheekWidth * 2) - size * 0.06

    // Blue LED display
    let ledRect = CGRect(x: innerX + innerW * 0.36, y: cardRect.minY + cardRect.height * 0.72, width: innerW * 0.28, height: cardRect.height * 0.13)
    let ledPath = CGPath(roundedRect: ledRect, cornerWidth: size * 0.02, cornerHeight: size * 0.02, transform: nil)
    ctx.addPath(ledPath)
    ctx.setFillColor(CGColor(red: 0.05, green: 0.45, blue: 0.95, alpha: 1.0))
    ctx.fillPath()

    // Draw "UF5" text inside/above LED
    let paragraph = NSMutableParagraphStyle()
    paragraph.alignment = .center
    let attrs: [NSAttributedString.Key: Any] = [
        .font: NSFont.systemFont(ofSize: size * 0.085, weight: .bold),
        .foregroundColor: NSColor.white,
        .paragraphStyle: paragraph
    ]
    let title = NSString(string: "UF5")
    title.draw(in: CGRect(x: ledRect.minX, y: ledRect.minY + size * 0.01, width: ledRect.width, height: ledRect.height), withAttributes: attrs)

    // Draw faders on left and knobs on right
    let numFaders = 4
    let faderSpacing = (innerW * 0.32) / CGFloat(numFaders)
    for i in 0..<numFaders {
        let fx = innerX + CGFloat(i) * faderSpacing + faderSpacing * 0.35
        let fy = cardRect.minY + cardRect.height * 0.52
        let fh = cardRect.height * 0.28
        ctx.setFillColor(CGColor(red: 0.35, green: 0.37, blue: 0.40, alpha: 1.0))
        ctx.fill(CGRect(x: fx, y: fy, width: size * 0.014, height: fh))
        let thumbY = fy + fh * (0.25 + 0.18 * CGFloat(i % 3))
        ctx.setFillColor(CGColor(red: 0.90, green: 0.92, blue: 0.95, alpha: 1.0))
        ctx.fill(CGRect(x: fx - size * 0.012, y: thumbY, width: size * 0.038, height: size * 0.024))
    }

    // Draw knobs on right
    let knobStartX = innerX + innerW * 0.68
    let knobSpacing = (innerW * 0.32) / 2.0
    for r in 0..<2 {
        for c in 0..<2 {
            let kx = knobStartX + CGFloat(c) * knobSpacing + knobSpacing * 0.25
            let ky = cardRect.minY + cardRect.height * (0.54 + CGFloat(r) * 0.14)
            let kr = size * 0.042
            ctx.setFillColor(CGColor(red: 0.25, green: 0.27, blue: 0.30, alpha: 1.0))
            ctx.fillEllipse(in: CGRect(x: kx, y: ky, width: kr * 2, height: kr * 2))
            ctx.setStrokeColor(CGColor(red: 0.0, green: 0.75, blue: 1.0, alpha: 1.0))
            ctx.setLineWidth(size * 0.012)
            ctx.strokeEllipse(in: CGRect(x: kx, y: ky, width: kr * 2, height: kr * 2))
        }
    }

    // Piano keys at bottom
    let keysY = cardRect.minY + cardRect.height * 0.08
    let keysH = cardRect.height * 0.36
    let numWhiteKeys = 10
    let whiteKeyW = innerW / CGFloat(numWhiteKeys)

    for k in 0..<numWhiteKeys {
        let kx = innerX + CGFloat(k) * whiteKeyW
        let kRect = CGRect(x: kx + size * 0.003, y: keysY, width: whiteKeyW - size * 0.006, height: keysH)
        ctx.setFillColor(CGColor(red: 0.96, green: 0.97, blue: 0.99, alpha: 1.0))
        ctx.fill(kRect)
    }

    let blackPattern = [0, 1, 3, 4, 5, 7, 8]
    let blackW = whiteKeyW * 0.62
    let blackH = keysH * 0.60
    for k in blackPattern {
        let bx = innerX + CGFloat(k + 1) * whiteKeyW - blackW * 0.5
        let bRect = CGRect(x: bx, y: keysY + keysH - blackH, width: blackW, height: blackH)
        ctx.setFillColor(CGColor(red: 0.10, green: 0.11, blue: 0.13, alpha: 1.0))
        ctx.fill(bRect)
    }

    ctx.restoreGState()

    // Outer subtle border
    ctx.addPath(bgPath)
    ctx.setStrokeColor(CGColor(red: 1.0, green: 1.0, blue: 1.0, alpha: 0.18))
    ctx.setLineWidth(size * 0.015)
    ctx.strokePath()

    img.unlockFocus()
    return img
}

func savePNG(_ image: NSImage, to path: String, pixelSize: Int) {
    guard let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: pixelSize,
        pixelsHigh: pixelSize,
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0
    ) else { return }

    rep.size = NSSize(width: pixelSize, height: pixelSize)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    image.draw(in: NSRect(x: 0, y: 0, width: pixelSize, height: pixelSize))
    NSGraphicsContext.restoreGraphicsState()

    if let data = rep.representation(using: .png, properties: [:]) {
        try? data.write(to: URL(fileURLWithPath: path))
    }
}

let fm = FileManager.default
let iconsetPath = "build/AppIcon.iconset"
try? fm.createDirectory(atPath: iconsetPath, withIntermediateDirectories: true)

let sizes = [16, 32, 64, 128, 256, 512, 1024]
for s in sizes {
    let img = drawCMEUF5Icon(size: CGFloat(s))
    if s <= 512 {
        savePNG(img, to: "\(iconsetPath)/icon_\(s)x\(s).png", pixelSize: s)
    }
    if s >= 32 {
        let half = s / 2
        savePNG(img, to: "\(iconsetPath)/icon_\(half)x\(half)@2x.png", pixelSize: s)
    }
}

let img128 = drawCMEUF5Icon(size: 128)
let img256 = drawCMEUF5Icon(size: 256)
savePNG(img128, to: "logic/UF5.device/icon.png", pixelSize: 128)
savePNG(img256, to: "logic/UF5.device/icon@2x.png", pixelSize: 256)

