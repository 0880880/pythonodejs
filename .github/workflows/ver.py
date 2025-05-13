import os,re;f='pyproject.toml';v=os.getenv('VERSION');t=open(f).read();open(f,'w').write(re.sub(r'(\[project\][\s\S]*?version\s*=\s*)\"[^\"]*\"', fr'\1\"{v}\"', t, flags=re.M))
