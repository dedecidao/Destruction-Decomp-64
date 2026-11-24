# Carros: registro de status, marchas e o que cada valor faz

Este documento lista tudo o que o jogo guarda de cada carro, com:
- os valores originais dos 24 carros;
- como o motor e o câmbio funcionam;
- o efeito esperado ao mudar cada valor.

Os números foram lidos do registro de cada carro na ROM (`DD64_DUMPCARS`) e o câmbio foi confirmado com log de corrida (`DD64_GEARLOG`). Nada aqui altera o jogo: o modo Play Fidelity usa os valores originais.

## Onde ficam

| O quê | Onde |
|---|---|
| Registro de cada carro | Recurso `D_80091AB4[carro]` (1000, 1100, 1200 ... 3300; um por carro, de 0 a 23), com 0x2B0 bytes (`CarStats`, `include/car.h`) |
| Cópia no carro em corrida | `Car_Create` (`src/auto/1E308.c`) copia o registro para `Car+0xBEC` e passa o bloco a partir de `mass` para `RigidBody_Init` |
| Curva de torque e câmbio | `Car+0x194` (cópia do registro a partir de +0x20; `Spline_ComputeCoeffs`) |
| Leitor no port | `port/game/wm_cars.c`: `WM_CarRecord(id)` |
| Barras da tela de seleção | `CarSelect_SetStatBars` (`src/auto/42D14.c`), uma tabela fixa no código, que não é calculada do registro |
| Texto de pontos fortes e fracos | `D_8009694C[k]` (`src/auto/42A44.c`): `k = 0` para os carros 1-12, `k = carro - 11` para os outros |

## Os 12 carros iniciais são iguais

No jogo original, os carros 1 a 12 têm os mesmos valores de vida, massa, motor, câmbio, tração, freio, direção, suspensão e zonas de dano. Entre eles, só mudam:
- o modelo;
- a posição das rodas e dos faróis;
- o tamanho da caixa de colisão;
- o ângulo do volante em alta velocidade (8 nos carros ímpares, 7 nos pares);
- dois números da zona 9 (nos carros 1 e 3).

Por isso a tela de seleção mostra as mesmas barras (14/9/19/11) e o mesmo texto para os 12. Isso não é bug de exibição. Só os desbloqueáveis (13 a 24) têm números próprios.

## Mapa do registro (CarStats, 0x2B0 bytes)

| Offset | Campo | Valor nos carros 1-12 | O que faz | Efeito de aumentar |
|---|---|---|---|---|
| 0x004 | modelo | 0-23 | O número do carro (o modelo 3D e a tabela de recursos) | Nenhum na física |
| 0x008 | vida total | 130 | A vida geral (0-200). `Car_Create` soma um extra conforme dificuldade e modo | Aguenta mais até ser destruído |
| 0x00C | tração | 1.0 | Aderência dos pneus | Segura mais em curva e patina menos na arrancada |
| 0x010 | freio | 2.0 | Força do freio | Para em menos espaço; demais trava as rodas |
| 0x014 | freio-motor | 1.6 | Desaceleração ao soltar o acelerador | Perde velocidade mais rápido sem frear |
| 0x018 | offroad | 0.9 | Aderência fora do asfalto | Perde menos na terra e na grama |
| 0x01C | resposta da direção | 2.1 | Rapidez do volante (`Car+0xB90`) | Vira mais rápido; demais fica nervoso |
| 0x024 | rpm máximo | 6500 | Limite do motor; o câmbio troca a 85% e a 25% dele | Marchas mais longas e velocidade final maior |
| 0x028 | rpm de marcha lenta | 750 | rpm mínimo | Quase nada |
| 0x02C | torque a 0 rpm | 165 | Início da curva de torque | Arrancada mais forte |
| 0x030 | torque máximo | 170 | Pico da curva | Acelera mais em todas as marchas |
| 0x034 | rpm do torque máximo | 3500 | Onde fica o pico | Muda em que rpm o carro "puxa" mais |
| 0x03C | nº de marchas | 6 | R + 5 marchas | (não mexer) |
| 0x040-0x054 | relações R, 1ª-5ª | -3.8, 12, 10.5, 9, 7, 2.5 | Multiplicam a força e o rpm em cada marcha | Mais força na marcha, mas chega antes ao limite dela |
| 0x058 | relação final | 1.9 | Multiplica todas as marchas | Mais arrancada e menos velocidade final |
| 0x05C | nº de marchas do som | 6 | | (não mexer) |
| 0x060-0x074 | trocas do som (mph) | -80, 30, 60, 90, 115, 135 | Em que velocidade o **som** do motor troca de marcha. Não muda a força | Só o som |
| 0x078 | massa | 910 | Massa em kg (o jogo limita a 500-2000) | Acelera e freia pior, mas empurra mais nas batidas |
| 0x080 | escala | 0.01 | Escala do modelo | (não mexer) |
| 0x084 | meia-caixa (comprimento, altura, largura) | 2.61, 0.91, 1.11 | Tamanho na colisão e inércia de rotação | Carro "maior" nas batidas e gira mais devagar |
| 0x090 | ângulo do volante lento | 65 | Ângulo das rodas parado | Vira mais fechado em baixa velocidade |
| 0x094 | ângulo do volante rápido | 8 / 7 | Ângulo a 100 km/h; entre os dois o jogo interpola numa curva | Vira mais em alta velocidade |
| 0x0B0 | mola K | 41000 | Rigidez da suspensão | Balança menos |
| 0x0B4 | amortecedor B | 0.65 | Amortecimento | Quica menos |
| 0x0B8 | altura (equilibrium) | 0.44 | Altura de repouso | Mais alto |
| 0x0BC / 0x0C0 | mola mín. / máx. | 0.18 / 0.46 | Curso da suspensão | |
| 0x0C8-0x124 | rodas | | Posições das rodas | Visual e contato com o chão |
| 0x128 / 0x170 | faróis | | Lanternas traseiras e faróis dianteiros | Visual (a luz dos faróis sai daqui) |
| 0x1E8-0x2AC | zonas de dano (10 × 0x14) | veja a tabela | Copiadas para `Car+0xA68`: id, defesa, ataque, resistência, 0 | |

Zonas, em ordem: frente, dianteira direita, lateral direita, traseira direita, dianteira esquerda, lateral esquerda, traseira esquerda, traseira, e as zonas 8 e 9 (ainda não identificadas).
- **Defesa** (0-99) reduz o dano que a zona recebe.
- **Ataque** (0-99) aumenta o dano que ela causa.
- **Resistência** é quantos golpes a zona aguenta. Quando a resistência da frente e das dianteiras cai, os faróis falham e quebram (Wrecked Metal).

## Motor e câmbio (confirmado com log)

São dois câmbios independentes.

**1. O câmbio da física.** É ele que move o carro (`Car_SelectGear`, `src/auto/3A250.c`).
- **rpm:** velocidade da roda × relação da marcha × relação final.
- **Trocas:** sobe acima de 85% do rpm máximo e desce abaixo de 25%, tudo automático. Não há embreagem nem tempo de troca.
- **Força** (`Car_GetDriveTorque`): curva de torque(rpm) × relação da marcha × relação final × 0.85. A força cai a cada troca.
- **Onde fica:** `Car+0xA58` guarda a marcha (0 = ré, 1 a 5) e `Car+0xA54` o rpm.

Log do carro 1 com o acelerador no fundo (`DD64_GEARLOG`):

| Momento | Marcha | Velocidade |
|---|---|---|
| Troca 1ª → 2ª | 2 | ~25 km/h (fica alternando 1ª↔2ª enquanto a roda patina) |
| Troca → 3ª | 3 | ~45 km/h |
| Troca → 4ª | 4 | ~52 km/h |
| Troca → 5ª | 5 | ~67 km/h (todas as trocas em ~1 segundo) |
| Velocidade final | 5 | ~216 km/h (134.5 mph), com rpm 6414 de 6500 |

Como a 5ª tem relação 2.5 e a 4ª tem 7, o carro passa quase toda a corrida em 5ª, e a força cai para cerca de um terço nessa troca.

**2. O câmbio do som.** É o que a gente ouve (`Car_UpdateEngineRpm`, `src/auto/3A558.c`).
- **Velocidade:** a do carro em mph (m/s × 2.235).
- **Marcha:** sai da tabela 0x060, com trocas em 30, 60, 90 e 115 mph.
- **rpm do som:** 1000 até 30 mph. Depois começa em 2500 + 400 por marcha e sobe até 70% do rpm máximo, com teto de 80%.
- **Onde fica:** `Car+0xBA4` guarda a marcha do som e `Car+0xBA8` o rpm.

No log, cada troca do som é uma queda de cerca de 800 rpm (4489 → 3702 na troca para a 5ª do som). A queda é pequena e não vem com nenhum corte de aceleração, por isso quase não se ouve. Há também um caso especial: com acelerador e duas ou mais rodas fora do chão, o rpm do som só sobe (+200 por frame) até o máximo.

## Valores originais dos 24 carros

A coluna "1-12" vale para todos os carros iniciais. Na linha "Som: troca em (mph)", são as velocidades em que o som troca de marcha.

Os números são os da ROM. A coluna "1-12" vale para todos os carros liberados de início.

| Campo | 1-12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Vida total (health) | 130 | 100 | 140 | 120 | 140 | 125 | 140 | 100 | 130 | 100 | 150 | 140 | 140 |
| Massa (kg) | 910 | 600 | 1130 | 1350 | 1000 | 1490 | 910 | 1060 | 1000 | 2000 | 1100 | 980 | 1000 |
| Torque a 0 rpm | 165 | 190 | 300 | 200 | 170 | 195 | 255 | 185 | 325 | 215 | 70 | 215 | 305 |
| Torque máximo | 170 | 265 | 500 | 220 | 180 | 230 | 455 | 225 | 415 | 330 | 135 | 350 | 335 |
| Torque/massa ×1000 | 186.813 | 441.667 | 442.478 | 162.963 | 180 | 154.362 | 500 | 212.264 | 415 | 165 | 122.727 | 357.143 | 335 |
| Relação R | -3.8 | -3.8 | -3.8 | -7.8 | -3.8 | -3.8 | -6.1 | -3.8 | -3.8 | -7.8 | -9.3 | -3.8 | -3.8 |
| Relação 1ª | 12 | 10.5 | 11.8 | 12 | 11.5 | 11.8 | 16.4 | 10.1 | 17.4 | 15.5 | 13.5 | 12 | 11.1 |
| Relação 2ª | 10.5 | 8.7 | 9.3 | 10.5 | 9.6 | 9.9 | 10.4 | 9 | 12 | 12 | 11.5 | 10.5 | 9.6 |
| Relação 3ª | 9 | 7 | 8 | 8 | 8.1 | 8.2 | 7.7 | 7.7 | 10.4 | 9.9 | 9 | 9 | 8.1 |
| Relação 4ª | 7 | 3.5 | 6.6 | 5.5 | 6.1 | 6.3 | 5.5 | 6 | 8 | 7.7 | 7 | 7 | 6 |
| Relação 5ª | 2.5 | 2.6 | 2.9 | 3 | 2.5 | 2.8 | 2.7 | 2.6 | 3.1 | 3.5 | 2.5 | 2.5 | 2.5 |
| Relação final | 1.9 | 2.1 | 1.7 | 1.8 | 2 | 1.9 | 1.8 | 1.6 | 1.45 | 1.8 | 1.9 | 1.9 | 1.8 |
| Som: troca em (mph) 1ª | 30 | 30 | 30 | 30 | 30 | 30 | 25 | 30 | 30 | 25 | 25 | 30 | 30 |
| Som: troca em (mph) 2ª | 60 | 50 | 60 | 60 | 50 | 60 | 40 | 60 | 70 | 50 | 40 | 60 | 60 |
| Som: troca em (mph) 3ª | 90 | 70 | 90 | 90 | 70 | 90 | 60 | 90 | 100 | 80 | 65 | 90 | 90 |
| Som: troca em (mph) 4ª | 115 | 90 | 115 | 115 | 90 | 115 | 95 | 115 | 130 | 110 | 90 | 115 | 115 |
| Som: troca em (mph) 5ª (máx.) | 135 | 110 | 135 | 135 | 110 | 135 | 115 | 135 | 155 | 150 | 110 | 135 | 135 |
| Tração (traction) | 1 | 1.7 | 1 | 0.75 | 1 | 1 | 1 | 1 | 1 | 1.2 | 1 | 1 | 1 |
| Freio (braking) | 2 | 1.7 | 2 | 1.4 | 1.2 | 1.4 | 2 | 2 | 2 | 1 | 0.5 | 1.7 | 3 |
| Freio-motor | 1.6 | 1.6 | 2 | 1.3 | 1.2 | 0.8 | 1.6 | 1 | 3 | 2 | 0.9 | 1.9 | 2 |
| Fora da pista (offroad) | 0.9 | 3 | 1 | 0.9 | 1.7 | 0.6 | 0.9 | 1.8 | 1 | 3 | 1.2 | 0.9 | 1 |
| Resposta da direção | 2.1 | 0.9 | 1.1 | 0.7 | 0.8 | 0.5 | 0.9 | 0.76 | 1.3 | 0.6 | 0.9 | 0.6 | 1.2 |
| Ângulo volante lento | 65 | 50 | 50 | 45 | 45 | 60 | 50 | 55 | 45 | 50 | 60 | 45 | 45 |
| Ângulo volante rápido | 8 / 7 | 7 | 7 | 6 | 8 | 7 | 6 | 6.5 | 7 | 6 | 6 | 7 | 6 |
| Mola K | 41000 | 17000 | 51000 | 40000 | 45000 | 45000 | 41000 | 50000 | 45000 | 90000 | 50000 | 44000 | 45000 |
| Amortecedor B | 0.65 | 0.45 | 0.65 | 0.45 | 0.65 | 0.45 | 0.65 | 0.65 | 0.65 | 0.65 | 0.65 | 0.65 | 0.65 |
| Altura (equilibrium) | 0.44 | 0.52 | 0.44 | 0.58 | 0.44 | 0.5 | 0.44 | 0.44 | 0.44 | 0.44 | 0.44 | 0.44 | 0.44 |
| Mola máx. | 0.46 | 0.52 | 0.46 | 0.58 | 0.46 | 0.5 | 0.46 | 0.46 | 0.46 | 0.46 | 0.46 | 0.46 | 0.46 |
| Meio-comprimento × altura × largura | 2.61×0.91×1.11 | 2.5×0.65×1.2 | 2.5×0.65×1.05 | 2.5×0.65×1.05 | 2.5×0.65×1.05 | 2.5×0.65×1.05 | 2.5×0.65×1.05 | 2.5×0.65×1.05 | 2.61×0.91×1.11 | 3×0.65×1.2 | 2.5×0.65×1.05 | 2.5×0.65×1.05 | 2.5×0.65×1.05 |
| Barras: vel/acel/contr/dur | 14/9/19/11 | 8/18/15/9 | 12/17/17/8 | 9/8/9/14 | 11/9/12/15 | 10/7/5/13 | 13/19/13/7 | 19/11/11/10 | 17/16/18/16 | 4/5/7/19 | 13/3/14/17 | 14/15/10/6 | 16/14/16/12 |

### Zonas de dano

Cada célula traz três valores: **defesa / ataque / resistência**.

- **Defesa** (0-99): quanto a zona segura o dano que recebe.
- **Ataque** (0-99): quanto dano ela causa ao bater em outro carro.
- **Resistência**: quantos golpes a zona aguenta até ficar destruída. É também ela que quebra os faróis.

As zonas 8 e 9 existem no registro, mas ainda não descobri a que parte do carro correspondem.

| Zona (defesa/ataque/resistência) | 1-12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Frente | 15/75/3 | 50/45/3 | 50/75/3 | 20/20/3 | 70/75/3 | 90/90/12 | 35/70/3 | 30/70/3 | 75/75/3 | 99/99/2 | 80/35/3 | 25/85/12 | 80/80/3 |
| Dianteira dir. | 20/75/3 | 45/45/3 | 50/75/3 | 20/20/3 | 70/75/3 | 85/85/10 | 35/70/3 | 30/65/3 | 80/70/3 | 99/99/2 | 80/35/3 | 25/85/10 | 70/75/3 |
| Lateral dir. | 70/35/3 | 50/50/3 | 40/55/3 | 90/85/3 | 75/60/3 | 35/40/10 | 45/65/3 | 75/65/3 | 75/65/3 | 99/99/2 | 80/35/3 | 75/35/10 | 10/15/2 |
| Traseira dir. | 80/65/3 | 50/55/3 | 50/55/3 | 80/85/3 | 80/85/3 | 20/30/10 | 45/65/3 | 80/65/3 | 85/65/3 | 99/99/2 | 80/35/3 | 80/65/10 | 40/50/3 |
| Dianteira esq. | 20/75/3 | 45/45/3 | 50/75/3 | 20/20/3 | 70/75/3 | 85/85/10 | 35/70/3 | 30/65/3 | 80/70/3 | 99/99/2 | 80/35/3 | 25/85/10 | 70/75/3 |
| Lateral esq. | 70/35/3 | 50/50/3 | 40/55/3 | 90/85/3 | 75/60/3 | 35/40/10 | 45/65/3 | 75/65/3 | 75/65/3 | 99/99/2 | 80/35/3 | 75/35/10 | 10/15/2 |
| Traseira esq. | 80/65/3 | 50/55/3 | 50/55/3 | 80/85/3 | 80/85/3 | 20/30/10 | 45/65/3 | 80/65/3 | 85/65/3 | 99/99/2 | 80/35/3 | 80/65/10 | 40/50/3 |
| Traseira | 65/70/3 | 45/55/3 | 50/55/3 | 90/95/3 | 80/85/3 | 20/30/12 | 45/65/3 | 80/75/3 | 80/70/3 | 99/99/2 | 80/35/3 | 80/70/12 | 50/60/3 |
| Zona 8 | 50/50/5 | 40/50/5 | 40/45/5 | 30/30/5 | 50/50/4 | 45/50/20 | 45/65/5 | 50/60/5 | 50/50/5 | 99/99/5 | 50/40/5 | 50/50/20 | 30/50/5 |
| Zona 9 | 5/99/5 | 50/50/5 | 40/90/5 | 30/30/5 | 5/99/4 | 15/90/20 | 5/99/5 | 5/99/5 | 45/75/5 | 50/99/5 | 35/40/5 | 5/99/20 | 55/90/5 |

## Ferramentas de teste (PC, só com a variável ligada)

| Variável | O que faz |
|---|---|
| `DD64_DUMPCARS=arquivo` | Grava o registro inteiro dos 24 carros, palavra por palavra (hex, inteiro e float) |
| `DD64_GEARLOG=1` | Registra no log, para o carro do jogador, a marcha e o rpm da física, a marcha e o rpm do som e a velocidade, a cada troca e a cada 15 frames |
