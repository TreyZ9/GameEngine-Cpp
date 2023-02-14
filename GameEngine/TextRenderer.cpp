#include "TextRenderer.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftglyph.h>

#include <algorithm>
#include <format>
#include <vector>

#include "OpenGLFunctions.h"
#include "TextShader.h"
#include "Config.h"
#include "Camera.h"
#include "Debug.h"
#include "Maths.h"

Font::Font()
{
	this->textureWidth = 0;
	this->textureHeight = 0;
	this->lineHeight = 0;
	this->textureID = 0;
}

Font::Font(const std::string& fontName)
{
	// init freetype
	FT_Library ft;
	LOG_freetypeLibraryLoad(!FT_Init_FreeType(&ft));
	FT_Face face;
	LOG_freetypeFontLoad(!FT_New_Face(ft, std::format("C:\\Windows\\fonts\\{}.ttf", fontName).c_str(), 0, &face), std::format("font:{}", fontName));

	// save line height
	this->lineHeight = (face->height >> 6) / 1.5f;

	// cache bitmaps and advance to populate texture atlas
	std::vector<FT_BitmapGlyph> bitmaps;
	std::vector<glm::ivec2> advance;
	this->textureWidth = 0;
	this->textureHeight = 0;
	for (unsigned char c = 0; c < 128; c++)
	{
		// load char
		if (FT_Load_Char(face, c, FT_LOAD_RENDER))
		{
			LOG_freetypeCharacterLoad(false, "");
			continue;
		}

		// render bitmap
		FT_Glyph glyph;
		FT_Get_Glyph(face->glyph, &glyph);
		if (glyph->format != FT_GLYPH_FORMAT_BITMAP)
			FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, 0, 1);
		FT_BitmapGlyph glyphBitmap = (FT_BitmapGlyph)glyph;

		bitmaps.push_back(glyphBitmap);
		advance.push_back(glm::ivec2(face->glyph->advance.x, face->glyph->advance.y));
		this->textureWidth += glyphBitmap->bitmap.width + 2; // add 2px buffer to help with aa bleed
		this->textureHeight = std::max(this->textureHeight, glyphBitmap->bitmap.rows);
	}

	// create empty texture atlas
	this->textureID;
	glCall(glGenTextures, 1, &this->textureID);
	glCall(glBindTexture, GL_TEXTURE_2D, this->textureID);
	glCall(glPixelStorei, GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, this->textureWidth, this->textureHeight, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
	glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glCall(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// populate atlas with textures and create character map
	int x = 0;
	for (unsigned int i = 0; i < 128; i++)
	{
		glCall(glTexSubImage2D, GL_TEXTURE_2D, 0, x, 0, bitmaps[i]->bitmap.width, bitmaps[i]->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, bitmaps[i]->bitmap.buffer);

		Character character = {
			x,
			glm::ivec2(bitmaps[i]->bitmap.width, bitmaps[i]->bitmap.rows),
			glm::ivec2(bitmaps[i]->left, bitmaps[i]->top),
			advance[i]
		};
		this->characters.insert(std::pair<char, Character>((char)i, character));
		x += bitmaps[i]->bitmap.width + 2; // 2px buffer
	}
}

Font::~Font() = default;

TextRenderer::TextRenderer()
{
	this->font = Font("arial");

	// create render object
	glCall(glGenVertexArrays, 1, &this->vao);
	glCall(glGenBuffers, 1, &this->vbo);
	glCall(glBindVertexArray, this->vao);
	glCall(glBindBuffer, GL_ARRAY_BUFFER, this->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	glCall(glEnableVertexAttribArray, 0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	glCall(glBindBuffer, GL_ARRAY_BUFFER, 0);
	glCall(glBindVertexArray, 0);
}

TextRenderer::~TextRenderer() {}

std::vector<std::string> TextRenderer::splitString(const std::string& text, const std::string& delimiter)
{
	std::vector<std::string> elements;
	size_t last = 0;
	size_t next = 0;
	while ((next = text.find(delimiter, last)) != std::string::npos)
	{
		elements.push_back(text.substr(last, next - last));
		last = next + 1;
	}
	elements.push_back(text.substr(last));
	return elements;
}

void TextRenderer::drawText(const std::string& text, glm::vec2 pos, glm::vec2 scale, std::string alignment, std::string origin)
{
	// bind vao and texture atlas
	glCall(glActiveTexture, GL_TEXTURE0);
	glCall(glBindVertexArray, this->vao);
	glCall(glBindTexture, GL_TEXTURE_2D, this->font.textureID);

	// ensure alignment is valid default:left
	if (alignment != "left" && alignment != "center" && alignment != "right")
	{
		alignment = "left";
	}

	// ensure origin is valid default:center
	if (origin != "topleft" && origin != "top" && origin != "topright" &&
		origin != "left" && origin != "center" && origin != "right" &&
		origin != "bottomleft" && origin != "bottom" && origin != "bottomright")
	{
		origin = "center";
	}

	// calculate bounding box
	glm::vec2 size = glm::vec2(0.0f);

	std::vector<std::string> lines = this->splitString(text, "\n");
	std::vector<float> lineOffsets;
	for (std::string line : lines)
	{
		float lineWidth = 0.0f;

		for (char character : line)
		{
			lineWidth += (this->font.characters.at(character).advance.x >> 6) * scale.x;
		}

		lineOffsets.push_back(lineWidth);
		size.x = std::max(size.x, lineWidth);
	}
	size.y = this->font.lineHeight * lines.size();

	// alignment
	for (int i = 0; i < lineOffsets.size(); i++)
	{
		if (alignment == "left")
			lineOffsets[i] = 0.0f;
		else if (alignment == "center")
			lineOffsets[i] = (size.x - lineOffsets[i]) / 2.0f;
		else if (alignment == "right")
			lineOffsets[i] = size.x - lineOffsets[i];
	}

	// origin horizontal
	for (int i = 0; i < lineOffsets.size(); i++)
	{
		if (origin == "top" || origin == "center" || origin == "bottom")
			lineOffsets[i] -= size.x / 2.0f;
		if (origin == "topright" || origin == "right" || origin == "bottomright")
			lineOffsets[i] -= size.x;
	}

	// origin vertical
	float yOffset = size.y - this->font.lineHeight;
	if (origin == "left" || origin == "center" || origin == "right")
		yOffset = (size.y / 2.0f) - this->font.lineHeight;
	if (origin == "topleft" || origin == "bottom" || origin == "topright")
		yOffset =  -(int)this->font.lineHeight;

	// draw each character
	for (int i = 0; i < lines.size(); i++)
	{
		float xOffset = 0.0f;
		for (char c : lines[i])
		{
			Character character = this->font.characters[c];

			float xpos = pos.x + xOffset + character.bearing.x * scale.x;
			float ypos = pos.y + yOffset - (character.size.y - character.bearing.y) * scale.y;

			float w = character.size.x * scale.x;
			float h = character.size.y * scale.y;

			float s0 = character.textureAtlasOffset / this->font.textureWidth;
			float s1 = (character.textureAtlasOffset + character.size.x) / this->font.textureWidth;
			float lineOffset = lineOffsets[i];

			float t1 = (float)character.size.y / (float)this->font.textureHeight;

			float vertices[6][4] = {
				{ xpos + lineOffset,     ypos + h,   s0, 0.0f },
				{ xpos + lineOffset,     ypos,       s0, t1 },
				{ xpos + w + lineOffset, ypos,       s1, t1 },

				{ xpos + lineOffset,     ypos + h,   s0, 0.0f },
				{ xpos + w + lineOffset, ypos,       s1, t1 },
				{ xpos + w + lineOffset, ypos + h,   s1, 0.0f }
			};

			glCall(glBindBuffer, GL_ARRAY_BUFFER, this->vbo);
			glCall(glBufferSubData, GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
			glCall(glBindBuffer, GL_ARRAY_BUFFER, 0);

			glDrawArrays(GL_TRIANGLES, 0, 6);

			xOffset += (character.advance.x >> 6) * scale.x;
		}

		yOffset -= this->font.lineHeight;
	}

	glCall(glBindVertexArray, 0);
	glCall(glBindTexture, GL_TEXTURE_2D, 0);
}

void TextRenderer::drawText(TextShader shader, const std::string& text, glm::vec3 pos, glm::vec3 rot, 
	glm::vec2 scale, glm::vec3 color, std::string alignment, std::string origin)
{
	shader.start();

	glm::mat4 projectionMatrix = glm::perspective(Config::Display::FOV, (float)Config::Display::WIDTH /
		(float)Config::Display::HEIGHT, Config::Display::NEAR_PLANE, Config::Display::FAR_PLANE);

	glm::mat4 transformationMatrix;
	Maths::createTransformationMatrix(transformationMatrix, pos, rot.x, rot.y, rot.z, 1.0f);

	shader.loadTransformationMatrix(transformationMatrix);
	shader.loadProjectionMatrix(projectionMatrix);
	shader.loadViewMatrix(Camera::viewMatrix);
	shader.loadTextColor(color);

	this->drawText(text, glm::vec2(0.0f), scale, alignment, origin);

	shader.stop();
}

void TextRenderer::drawTextOnHUD(TextShader shader, const std::string& text, glm::vec2 pos, 
	glm::vec2 scale, glm::vec3 color, std::string alignment, std::string origin)
{
	shader.start();

	glm::mat4 projectionMatrix = glm::ortho(0.0f, (float)Config::Display::WIDTH, 0.0f, (float)Config::Display::HEIGHT);
	shader.loadTransformationMatrix(glm::mat4(1.0f));
	shader.loadProjectionMatrix(projectionMatrix);
	shader.loadViewMatrix(glm::mat4(1.0f));
	shader.loadTextColor(color);

	this->drawText(text, pos, scale, alignment, origin);

	shader.stop();
}