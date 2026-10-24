#include "CharacterShader.hpp"
#include "TeamPalette.hpp"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>

namespace restoration {
namespace {
GLuint compile(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Source character ")+(type==GL_VERTEX_SHADER?"vertex":"fragment")+" shader compilation: "+log);
    }
    return shader;
}
template<glm::length_t L, class T, glm::qualifier Q>
std::vector<double> values(const glm::vec<L, T, Q>& value) {
    return {glm::value_ptr(value), glm::value_ptr(value) + L};
}
template<glm::length_t C, glm::length_t R, class T, glm::qualifier Q>
std::vector<double> values(const glm::mat<C, R, T, Q>& value) {
    return {glm::value_ptr(value), glm::value_ptr(value) + C * R};
}
}

CharacterShader::CharacterShader(const std::string& vertex, const std::string& fragment,bool linearOutput,bool environment,bool fog) {
    std::string sceneVertex=vertex,sceneFragment=fragment;
    if(environment){
        // Activate the already-expanded source chunks for the source 256-face PMREM atlas.
        const std::string features=std::string("\n#define USE_ENVMAP\n#define ENVMAP_TYPE_CUBE_UV\n#define CUBEUV_TEXEL_WIDTH (1.0 / 768.0)\n#define CUBEUV_TEXEL_HEIGHT (1.0 / 1024.0)\n#define CUBEUV_MAX_MIP 8.0\n")+(fog?"#define USE_FOG\n":"");
        sceneVertex.insert(sceneVertex.find('\n')+1,features);
        sceneFragment.insert(sceneFragment.find('\n')+1,features);
    }
    const GLuint vs = compile(GL_VERTEX_SHADER, sceneVertex);
    GLuint fs = 0;
    std::string lodFragment = sceneFragment;
    if(linearOutput){
        // Three's render-target materials bypass tone mapping and output encoding;
        // the final pass applies Neutral and sRGB exactly once.
        const std::string mapping="#define TONE_MAPPING\n";
        const auto at=lodFragment.find(mapping);
        if(at!=std::string::npos)lodFragment.erase(at,mapping.size());
        lodFragment=std::regex_replace(lodFragment,
            std::regex(R"(gl_FragColor\s*=\s*linearToOutputTexel\s*\(\s*gl_FragColor\s*\)\s*;)"),"");
    }
    const std::regex main(R"(void\s+main\s*\(\s*\)\s*\{)");
    const std::string dither = R"GLSL(
uniform vec2 uLodFade;
uniform bool iwDepthOnly;
float iwLodIGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void main() {
    if (uLodFade.x > 0.0) {
        float noise = iwLodIGN(floor(gl_FragCoord.xy));
        if ((noise < uLodFade.x) == (uLodFade.y > 0.0)) discard;
    }
)GLSL";
    if (!std::regex_search(lodFragment, main)) { glDeleteShader(vs); throw std::runtime_error("Source character fragment has no main"); }
    lodFragment = std::regex_replace(lodFragment, main, dither, std::regex_constants::format_first_only);
    // Preserve source alpha/hash clipping, then skip PBR work when only depth is written.
    const auto depthAt=lodFragment.find("// source chunk: normal_fragment_begin");
    if(depthAt!=std::string::npos)lodFragment.insert(depthAt,"if(iwDepthOnly){gl_FragColor=vec4(0.0);return;}\n");
    try { fs = compile(GL_FRAGMENT_SHADER, lodFragment); }
    catch (...) { glDeleteShader(vs); throw; }
    program_ = glCreateProgram();
    glAttachShader(program_, vs); glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program_, length, nullptr, log.data());
        glDeleteProgram(program_); program_ = 0;
        throw std::runtime_error("Source character shader link: " + log);
    }
    GLint count = 0, maxLength = 0;
    glGetProgramiv(program_, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program_, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    std::string name(static_cast<std::size_t>(maxLength), '\0');
    for (GLint i = 0; i < count; ++i) {
        GLsizei length = 0; GLint size = 0; GLenum type = 0;
        glGetActiveUniform(program_, static_cast<GLuint>(i), maxLength, &length, &size, &type, name.data());
        const std::string key = name.substr(0, static_cast<std::size_t>(length));
        const Uniform uniform{glGetUniformLocation(program_, key.c_str()), size, type};
        uniforms_[key] = uniform;
        if (key.size() > 3 && key.substr(key.size() - 3) == "[0]") uniforms_[key.substr(0, key.size() - 3)] = uniform;
    }
}

CharacterShader::~CharacterShader() {
    if (boneTexture_) glDeleteTextures(1, &boneTexture_);
    if (program_) {
        // A deleted current program must not escape into the next renderer's state guard.
        GLint current = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        if (static_cast<GLuint>(current) == program_) glUseProgram(0);
        glDeleteProgram(program_);
    }
}

void CharacterShader::use() const { glUseProgram(program_); }
int CharacterShader::attribute(const std::string& name) const { return glGetAttribLocation(program_, name.c_str()); }

void CharacterShader::uniform(const std::string& name, double value) const { uniform(name, std::vector<double>{value}); }

void CharacterShader::uniform(const std::string& name, const std::vector<double>& values) const {
    const auto found = uniforms_.find(name);
    if (found == uniforms_.end()) return; // The source shader optimizes unused material values away.
    const auto& u = found->second;
    int components = 1;
    switch (u.type) {
    case GL_FLOAT_VEC2: case GL_INT_VEC2: components = 2; break;
    case GL_FLOAT_VEC3: case GL_INT_VEC3: components = 3; break;
    case GL_FLOAT_VEC4: case GL_INT_VEC4: components = 4; break;
    case GL_FLOAT_MAT3: components = 9; break;
    case GL_FLOAT_MAT4: components = 16; break;
    default: break;
    }
    if (values.empty() || values.size() % components || values.size() / components > static_cast<std::size_t>(u.size))
        throw std::runtime_error("Invalid source uniform shape: " + name);
    const auto count = static_cast<GLsizei>(values.size() / components);
    std::vector<float> f(values.begin(), values.end());
    if(u.type==GL_FLOAT_VEC3&&values.size()==3&&(name=="uTeam"||name=="uTeamA"||name=="uTeamB")){
        for(int team=0;team<2;++team)if(glm::length(glm::dvec3(values[0],values[1],values[2])-SourceInkColors[team])<1e-9)
            for(int c=0;c<3;++c)f[c]=float(InkColors[team][c]);
    }
    switch (u.type) {
    case GL_FLOAT: glUniform1fv(u.location, count, f.data()); break;
    case GL_FLOAT_VEC2: glUniform2fv(u.location, count, f.data()); break;
    case GL_FLOAT_VEC3: glUniform3fv(u.location, count, f.data()); break;
    case GL_FLOAT_VEC4: glUniform4fv(u.location, count, f.data()); break;
    case GL_FLOAT_MAT3: glUniformMatrix3fv(u.location, count, GL_FALSE, f.data()); break;
    case GL_FLOAT_MAT4: glUniformMatrix4fv(u.location, count, GL_FALSE, f.data()); break;
    case GL_BOOL: case GL_INT:
    case GL_SAMPLER_2D: case GL_SAMPLER_CUBE: case GL_SAMPLER_2D_SHADOW: case GL_SAMPLER_2D_ARRAY:
    case GL_INT_SAMPLER_2D: case GL_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_2D:
    case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: {
        const std::vector<int> integers(values.begin(), values.end());
        glUniform1iv(u.location, count, integers.data()); break;
    }
    default: throw std::runtime_error("Unsupported active source uniform type: " + name);
    }
}

void CharacterShader::matrices(const glm::dmat4& model, const glm::dmat4& view, const glm::dmat4& projection, glm::dvec3 camera) const {
    const glm::dmat4 modelView = view * model;
    uniform("modelMatrix", values(model)); uniform("viewMatrix", values(view));
    uniform("modelViewMatrix", values(modelView)); uniform("projectionMatrix", values(projection));
    uniform("normalMatrix", values(glm::transpose(glm::inverse(glm::dmat3(modelView)))));
    uniform("cameraPosition", values(camera)); uniform("isOrthographic", 0.0);
}

void CharacterShader::lights(const CharacterLightState& light, const glm::dmat4& view) const {
    const glm::dmat3 rotation(view);
    uniform("ambientLightColor", values(light.ambient));
    uniform("directionalLights[0].direction", values(glm::normalize(rotation * glm::dvec3(light.sunDirection))));
    uniform("directionalLights[0].color", values(light.sunColor));
    uniform("hemisphereLights[0].direction", values(glm::normalize(rotation * glm::dvec3(light.skyDirection))));
    uniform("hemisphereLights[0].skyColor", values(light.skyColor)); uniform("hemisphereLights[0].groundColor", values(light.groundColor));
    uniform("toneMappingExposure", light.exposure);
    uniform("fogColor",values(light.fogColor));
    uniform("fogNear",light.fogNear);uniform("fogFar",light.fogFar);
    uniform("envMap",9.0);uniform("envMapIntensity",light.environmentTexture?light.environmentIntensity:0.0);
    uniform("envMapRotation",std::vector<double>{1,0,0,0,1,0,0,0,1});
    if(light.environmentTexture){
        GLint previous=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&previous);
        glActiveTexture(GL_TEXTURE9);glBindTexture(GL_TEXTURE_2D,light.environmentTexture);glActiveTexture(previous);
    }
}

void CharacterShader::material(const CharacterMaterialState& m) const {
    uniform("uTeam", values(m.team)); uniform("uShirt", values(m.shirt)); uniform("uShorts", values(m.shorts));
    uniform("uShoe", values(m.shoe)); uniform("uSole", values(m.sole)); uniform("uSock", values(m.sock)); uniform("uStrap", values(m.strap));
    uniform("uIris", values(m.iris)); uniform("uIris2", values(m.iris2));
    const auto flash = uniforms_.find("uFlash");
    if (flash == uniforms_.end() || flash->second.type != GL_FLOAT) uniform("uFlash", values(m.flash));
    const auto glow = uniforms_.find("uGlow");
    if (glow == uniforms_.end() || glow->second.type != GL_FLOAT) uniform("uGlow", values(m.glow));
    uniform("uHurt", values(m.hurt)); uniform("uHurtSeed", m.hurtSeed); uniform("uPattern", m.pattern);
    uniform("uMouth", values(m.mouth)); uniform("uMouth2", values(m.mouth2)); uniform("uLid", values(m.lid)); uniform("uGaze", values(m.gaze));
    uniform("uLook", values(m.look)); uniform("uPupil", m.pupil); uniform("uFreckle", m.freckle);
    uniform("uTime", m.time); uniform("uOpacity", m.opacity); uniform("uWig", values(m.wig));
    uniform("uIwRim", values(m.rim)); uniform("uIwRimL", values(m.rimLight)); uniform("uIwFill", values(m.fillLight));
}

void CharacterShader::bones(const std::array<glm::dmat4, CharacterBoneCount>& palette, unsigned textureUnit) {
    std::array<float, CharacterBoneCount * 16> data{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const auto* m = glm::value_ptr(palette[i]);
        for (std::size_t j = 0; j < 16; ++j) data[i * 16 + j] = static_cast<float>(m[j]);
    }
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    if (!boneTexture_) {
        glGenTextures(1, &boneTexture_); glBindTexture(GL_TEXTURE_2D, boneTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, static_cast<GLsizei>(CharacterBoneCount), 0, GL_RGBA, GL_FLOAT, data.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, boneTexture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, static_cast<GLsizei>(CharacterBoneCount), GL_RGBA, GL_FLOAT, data.data());
    }
    uniform("boneTexture", textureUnit);
    uniform("bindMatrix", values(glm::dmat4(1.0))); uniform("bindMatrixInverse", values(glm::dmat4(1.0)));
}

std::string CharacterShader::readSource(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read source character shader: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace restoration
